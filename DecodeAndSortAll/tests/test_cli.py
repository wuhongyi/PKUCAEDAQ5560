"""Failure-path integration checks against a real ROOT-linked executable."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()
params = dict(POL=1, THRE=80, TRIGFL=8, TRIGFLFG=16, BLLEN=128,
              XIAOFFSET=16, XIASL=625, XIASG=125, TAU=50000)
settings = dict(sync_config=3, channel_enable=0xffffffff,
                channels=[params.copy() for _ in range(32)])
header = dict(schema='r5560-list-v1', module_index=0, daq_id=7, run_id=12,
              api_version=0x20000, event_format=2, clock_hz=125000000,
              event_bytes=32, byte_order='little', simulated=True,
              settings=settings, created_utc='2026-01-01T00:00:00Z')

def binary(path, h=header, payload=None):
    j = json.dumps(h).encode()
    if payload is None:
        payload = struct.pack('<8I', 10000, 0, 12800, 12800, 62500, 12500, 63000, 0)
    path.write_bytes(b'R5560D1\n' + struct.pack('<I', len(j)) + j + payload)

def invoke(directory, name, args=(), ok=False, contains=''):
    output = directory / (name + '.root')
    result = subprocess.run([str(exe), '-o', str(output), *map(str,args)],
                            capture_output=True, text=True)
    assert (result.returncode == 0) == ok, (name,result.stdout,result.stderr)
    if contains:
        assert contains in result.stderr, (name,result.stderr)
    assert output.exists() == ok, name
    assert not list(directory.glob('r5560-sort-*')), name
    return output

with tempfile.TemporaryDirectory(prefix='r5560-cli-') as tmp:
    d = pathlib.Path(tmp)
    good = d/'event.bin'; binary(good)
    output=invoke(d,'good',[good],ok=True)
    original=output.read_bytes()
    r=subprocess.run([str(exe),'-o',str(output),str(good)],capture_output=True,text=True)
    assert r.returncode and output.read_bytes()==original
    invoke(d,'duplicate',[good,good],contains='Duplicate input file')
    invalid=d/'bad.bin'; binary(invalid,payload=bytes(31))
    invoke(d,'truncated',[invalid],contains='Truncated final')
    binary(invalid,payload=struct.pack('<8I',10000,0,0x07000000,0,0,0,0,0))
    invoke(d,'reserved',[invalid],contains='reserved event bits')
    binary(invalid,h=dict(header,clock_hz=100000000))
    invoke(d,'clock',[invalid],contains='125 MHz')
    binary(invalid,h=dict(header,event_format=1))
    invoke(d,'format',[invalid],contains='Unsupported')
    binary(invalid,h=dict(header,module_index=1,daq_id=8,run_id=13))
    invoke(d,'mixedrun',[good,invalid],contains='same run ID')
    invalid.unlink()
    run=d/'run';run.mkdir();b=run/'data.bin';binary(b)
    manifest=dict(schema='r5560-run-v1',run_id=12,status='incomplete',settings=[settings],
                  crate=dict(modules=1,daq_ids=[7]),results=[])
    mp=run/'run.json';mp.write_text(json.dumps(manifest))
    invoke(d,'incomplete',[run],contains='status=incomplete')
    invoke(d,'accepted-incomplete',['--allow-incomplete',run],ok=True)
    counts=[dict(channel=c,received_u64=str(1 if c==0 else 0)) for c in range(32)]
    manifest.update(status='complete',results=[dict(module=0,bytes='32',events='2',channel_counts=counts)])
    mp.write_text(json.dumps(manifest))
    invoke(d,'countmismatch',[run],contains='Event/byte count differs')
    manifest['results'][0]['events']='1';mp.write_text(json.dumps(manifest))
    invoke(d,'complete',[run],ok=True)
    manifest['crate']['modules']=2;mp.write_text(json.dumps(manifest))
    invoke(d,'missingmodule',[run],contains='missing a DAQ')
    par=d/'invalid.dat';par.write_text('0 32 0 0 1\n')
    invoke(d,'invalidpar',['--par',par,good],contains='invalid module/channel')
    par.write_text('0 0 9223372036854775807 -1e300 1e300\n')
    invoke(d,'overflow',['--par',par,good],contains='Timestamp offset overflow')
    par.write_text('0 0 -80001 -1e300 1e300\n')
    invoke(d,'negative-ts',['--par',par,good],ok=True)
    invoke(d,'nosort',['--no-sort',good],ok=True)
    invoke(d,'zlib',['--compression','zlib',good],ok=True)
    # Same run number is not sufficient to join independent acquisitions.
    a=d/'session-a';c=d/'session-b';a.mkdir();c.mkdir()
    binary(a/'m0.bin');binary(c/'m1.bin',h=dict(header,module_index=1,daq_id=8))
    common=dict(schema='r5560-run-v1',run_id=12,status='incomplete',settings=[settings,settings],
                crate=dict(modules=2,daq_ids=[7,8]),results=[])
    (a/'run.json').write_text(json.dumps(dict(common,started_utc='2026-01-01T00:00:00Z')))
    (c/'run.json').write_text(json.dumps(dict(common,started_utc='2026-01-02T00:00:00Z')))
    invoke(d,'different-sessions',['--allow-incomplete',a/'m0.bin',c/'m1.bin'],contains='different acquisition sessions')
    # Pipeline regression: later channel data outside retention must fail rather
    # than silently producing an unsorted ROOT tree. A larger window can sort it.
    def record(t, ch=0):
        return struct.pack('<8I', t & 0xffffffff, t >> 32, (ch << 27) | 12800,
                           12800, 62500, 12500, 63000, 0)
    late=d/'late.bin'
    binary(late,payload=record(100)+record(200)+record(1,1))
    invoke(d,'late-window',['--window-ns','400',late],contains='Late event')
    invoke(d,'late-larger-window',['--window-ns','4000',late],ok=True)
    invoke(d,'late-external',['--sort-mode','external',late],ok=True)
    duplicates=d/'duplicates.bin'
    binary(duplicates,payload=record(100)+record(100)+record(100))
    invoke(d,'ties',['--window-ns','1',duplicates],ok=True)
    sparse=d/'sparse.bin'
    binary(sparse,payload=record(0)+record((1<<48)-1))
    invoke(d,'long-gap',['--window-ns','1',sparse],ok=True)
    empty=d/'empty.bin';binary(empty,payload=b'')
    invoke(d,'empty-window',[empty],ok=True)
    # The frontier must advance across all files, including EOF and empty DAQs.
    m1=d/'m1.bin';binary(m1,h=dict(header,module_index=1,daq_id=8),payload=record(2)+record(3))
    m2=d/'m2.bin';binary(m2,h=dict(header,module_index=2,daq_id=9),payload=b'')
    invoke(d,'multiple-eof',['--window-ns','100',duplicates,m1,m2],ok=True)
    dense=d/'dense.bin';binary(dense,payload=record(100)*14000)
    invoke(d,'window-cap',['--memory-mib','1',dense],contains='memory limit')
    for flag, value in [('--window-ns','0'),('--write-fraction','0'),('--write-fraction','1.1'),
                        ('--write-fraction','nan'),('--sort-mode','invalid'),('--compression','invalid')]:
        invoke(d,'bad-option-'+flag[2:]+value,[flag,value,good])
print('PASS: CLI success/failure, no overwrite, strict integrity and temporary cleanup checks')
