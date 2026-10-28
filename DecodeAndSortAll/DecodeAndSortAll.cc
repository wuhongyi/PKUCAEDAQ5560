#include "R5560Decode.hh"
#include "Sort.hh"
#include "WindowSort.hh"
#include "UserDefine.hh"
#include <TFile.h>
#include <TTree.h>
#include <TObjString.h>
#include <TH1D.h>
#include <Compression.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace r5560;

static uint64_t unsignedValue(const json &v) {
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer() && v.get<int64_t>() >= 0) return uint64_t(v.get<int64_t>());
    if (v.is_string()) {
        const auto s = v.get<std::string>();
        if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos) {
            size_t n = 0; const auto value = std::stoull(s, &n);
            if (n == s.size()) return value;
        }
    }
    throw std::runtime_error("Expected nonnegative integer in run.json");
}
static json readJson(const fs::path &p) {
    if (fs::file_size(p) > 16*1024*1024) throw std::runtime_error("JSON exceeds 16 MiB: " + p.string());
    std::ifstream in(p);
    if (!in) throw std::runtime_error("Cannot open " + p.string());
    json j; in >> j; return j;
}
struct Options {
    std::vector<fs::path> inputs;
    fs::path output, temp, parameters;
    size_t memoryMiB = 256;
    bool allowIncomplete = false, sort = true;
    std::string sortMode = "window";
    std::string compression = "lz4";
    int64_t windowNs = TIMEBUFF;
    double writeFraction = PROCESSBUFF;
    bool runMode = false;
    uint32_t requestedRun = 0;
};
static void help() {
    std::cout << "R5560 DecodeAndSortAll: GUI5560 binary to CERN ROOT\n"
                 "Usage: DecodeAndSortAll [options] RUN_DIRECTORY | FILE.bin [FILE.bin ...]\n"
                 "       decodeandsort RUN_NUMBER  (paths in UserDefine.hh)\n"
                 "  -o, --output FILE.root    Output (default: runN.root in current directory)\n"
                 "  --sort-mode MODE          window (default), external or none\n"
                 "  --window-ns N             Rolling read window in ns (default TIMEBUFF=1e9)\n"
                 "  --write-fraction F        Fraction written per window (default PROCESSBUFF=0.9)\n"
                 "  --compression NAME        lz4 (default, faster) or zlib (smaller); level 4\n"
                 "  --memory-mib N            Container capacity in MiB (default 256)\n"
                 "  --temp-dir DIRECTORY      Temporary sort files (external mode only)\n"
                 "  --par FILE.dat            mod ch ts_offset_ns energy_low energy_high\n"
                 "  --allow-incomplete        Permit run.json status other than complete;\n"
                 "                            keep incomplete provenance; never skip corrupt events\n"
                 "  --no-sort                 Preserve module/file input order\n"
                 "  -h, --help                Show this help\n"
                 "Inputs must be GUI5560 R5560D1 files, from one run. No input is modified.\n"
                 "Existing output is never overwritten. Recovery/raw diagnostic files are unsupported.\n";
}
static Options parse(int argc, char **argv) {
    Options o;
    for (int i=1; i<argc; ++i) {
        std::string a=argv[i];
        auto argument = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after " + a);
            return argv[i];
        };
        if (a=="-o" || a=="--output") o.output=argument();
        else if (a=="--temp-dir") o.temp=argument();
        else if (a=="--par") o.parameters=argument();
        else if (a=="--compression") {
            o.compression=argument();
            if(o.compression!="lz4" && o.compression!="zlib")
                throw std::runtime_error("--compression must be lz4 or zlib");
        }
        else if (a=="--sort-mode") {
            o.sortMode=argument();
            if(o.sortMode!="window" && o.sortMode!="external" && o.sortMode!="none")
                throw std::runtime_error("--sort-mode must be window, external or none");
            o.sort=o.sortMode!="none";
        } else if (a=="--window-ns") {
            const auto n=unsignedValue(json(argument()));
            if(!n || n>uint64_t(std::numeric_limits<int64_t>::max()))
                throw std::runtime_error("--window-ns must be a positive int64");
            o.windowNs=static_cast<int64_t>(n);
        } else if (a=="--write-fraction") {
            const auto value=argument(); size_t end=0;
            o.writeFraction=std::stod(value,&end);
            if(end!=value.size()) throw std::runtime_error("Invalid --write-fraction");
        }
        else if (a=="--memory-mib") {
            const auto s=argument();
            const auto n=unsignedValue(json(s));
            if (n<1 || n>16384) throw std::runtime_error("--memory-mib must be 1..16384");
            o.memoryMiB=size_t(n);
        } else if (a=="--allow-incomplete") o.allowIncomplete=true;
        else if (a=="--no-sort") {o.sort=false;o.sortMode="none";}
        else if (a=="-h" || a=="--help") { help(); std::exit(0); }
        else if (!a.empty() && a[0]=='-') throw std::runtime_error("Unknown option " + a);
        else o.inputs.emplace_back(a);
    }
    if (o.inputs.empty()) throw std::runtime_error("Specify a run directory or binary file(s); use --help");
    if(o.windowNs<=0 || !std::isfinite(o.writeFraction) || o.writeFraction<=0 || o.writeFraction>1)
        throw std::runtime_error("TIMEBUFF/window-ns must be positive and PROCESSBUFF/write-fraction in (0,1]");
    if (o.inputs.size()==1) {
        auto s=o.inputs[0].string();
        if (!s.empty() && s.find_first_not_of("0123456789")==std::string::npos) {
            const auto n=unsignedValue(json(s));
            if(n>0xffffffffu) throw std::runtime_error("Run number exceeds uint32");
            o.runMode=true; o.requestedRun=uint32_t(n);
            o.inputs[0]=fs::path(RAWFILEPATH)/std::to_string(n);
            if(o.output.empty()) {
                std::ostringstream name; name<<ROOTFILENAME<<"_"<<std::setw(4)<<std::setfill('0')<<n<<"_notwave.root";
                o.output=fs::path(ROOTFILEPATH)/name.str();
            }
            if(o.parameters.empty()) o.parameters=PARAMETERFILE;
        }
    }
    return o;
}
struct ChannelRule {int64_t offset=0; double low=0,high=0;};
static std::map<std::pair<uint32_t,uint32_t>,ChannelRule> readRules(const fs::path &path) {
    std::map<std::pair<uint32_t,uint32_t>,ChannelRule> rules;
    if(path.empty()) return rules;
    std::ifstream in(path);
    if(!in) throw std::runtime_error("Cannot open parameter table "+path.string());
    std::string line; size_t lineNumber=0;
    while(std::getline(in,line)) {
        ++lineNumber; line=line.substr(0,line.find('#'));
        std::istringstream row(line); std::string mod,ch,offset,low,high,extra;
        if(!(row>>mod)) continue;
        if(mod=="mod") continue;
        try {
            if(!(row>>ch>>offset>>low>>high) || (row>>extra)) throw std::runtime_error("expected 5 columns");
            auto m=unsignedValue(json(mod)), c=unsignedValue(json(ch));
            if(m>65535 || c>=32) throw std::runtime_error("invalid module/channel");
            size_t end=0; ChannelRule rule;
            rule.offset=std::stoll(offset,&end);
            if(end!=offset.size()) throw std::runtime_error("invalid time offset");
            rule.low=std::stod(low,&end);
            if(end!=low.size() || !std::isfinite(rule.low)) throw std::runtime_error("invalid lower energy gate");
            rule.high=std::stod(high,&end);
            if(end!=high.size() || !std::isfinite(rule.high) || rule.low>rule.high) throw std::runtime_error("invalid upper energy gate");
            if(!rules.emplace(std::make_pair(uint32_t(m),uint32_t(c)),rule).second) throw std::runtime_error("duplicate channel rule");
        } catch(const std::exception &e) {throw std::runtime_error(path.string()+":"+std::to_string(lineNumber)+": "+e.what());}
    }
    if(in.bad()) throw std::runtime_error("Parameter table read failed");
    return rules;
}
struct WorkDirectory {
    fs::path path;
    explicit WorkDirectory(const fs::path &parent) {
        if (!fs::is_directory(parent)) throw std::runtime_error("Temporary parent directory does not exist");
        const auto tick=std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i=0;i<100;++i) {
            auto candidate=parent/("r5560-sort-"+std::to_string(tick)+"-"+std::to_string(i));
            if (fs::create_directory(candidate)) {path=candidate; return;}
        }
        throw std::runtime_error("Cannot create private temporary directory");
    }
    ~WorkDirectory() { std::error_code ec; if (!path.empty()) fs::remove_all(path,ec); }
};
struct Source {
    fs::path path;
    Header header;
    json manifest;
    uint64_t events=0;
    std::array<uint64_t,32> counts{};
};
static void checkManifest(Source &s, bool allowIncomplete) {
    const auto p=s.path.parent_path()/"run.json";
    if (!fs::exists(p)) {
        std::cerr<<"Warning: no run.json for "<<s.path<<"; acquisition completeness is unknown.\n";
        return;
    }
    s.manifest=readJson(p);
    if (s.manifest.at("schema")!="r5560-run-v1") throw std::runtime_error("Unsupported run.json schema");
    if (unsignedValue(s.manifest.at("run_id"))!=s.header.runId) throw std::runtime_error("run.json run_id mismatch");
    const auto state=s.manifest.value("status",std::string());
    if (state!="complete" && !allowIncomplete)
        throw std::runtime_error("run.json status="+state+"; use --allow-incomplete only for explicit analysis of an incomplete run");
    if (state!="complete") std::cerr<<"Warning: converting INCOMPLETE run; original status="<<state<<"\n";
    if (s.manifest.at("settings").at(s.header.module)!=s.header.json.at("settings"))
        throw std::runtime_error("Header settings disagree with run.json");
    if (s.manifest.at("crate").at("daq_ids").at(s.header.module)!=s.header.daqId)
        throw std::runtime_error("Header DAQ ID disagrees with run.json");
}
static void checkCounts(const Source &s) {
    if (s.manifest.is_null() || s.manifest.value("status",std::string())!="complete") return;
    const json *result=nullptr;
    for (const auto &r:s.manifest.at("results"))
        if (unsignedValue(r.at("module"))==s.header.module) {
            if (result) throw std::runtime_error("Duplicate module in run.json results");
            result=&r;
        }
    if (!result) throw std::runtime_error("Missing module result in complete run.json");
    if (unsignedValue(result->at("bytes"))!=s.header.payloadBytes || unsignedValue(result->at("events"))!=s.events)
        throw std::runtime_error("Event/byte count differs from complete run.json for module "+std::to_string(s.header.module));
    if (result->contains("channel_counts")) {
        std::set<uint64_t> seen;
        for (const auto &row:result->at("channel_counts")) {
            auto ch=unsignedValue(row.at("channel"));
            if (ch>=32 || !seen.insert(ch).second || unsignedValue(row.at("received_u64"))!=s.counts[ch])
                throw std::runtime_error("Channel count differs from complete run.json");
        }
        if (seen.size()!=32) throw std::runtime_error("Missing channel counts in complete run.json");
    }
    if (result->value("transport",std::string())=="dma-zmq") {
        const auto tx=unsignedValue(result->at("dma_tx_words64")), ack=unsignedValue(result->at("dma_ack_words64"));
        if (tx!=ack || tx!=s.header.payloadBytes/8 || result->value("dma_pending",true))
            throw std::runtime_error("DMA accounting differs from complete run.json");
    }
}
static int convert(const Options &options) {
    const auto conversionStart=std::chrono::steady_clock::now();
    std::vector<fs::path> paths;
    std::set<fs::path> directories;
    for (const auto &input:options.inputs) {
        if (fs::is_directory(input)) {
            auto dir=fs::canonical(input); directories.insert(dir);
            std::vector<fs::path> files;
            for (const auto &entry:fs::directory_iterator(dir))
                if (entry.is_regular_file() && entry.path().extension()==".bin") files.push_back(entry.path());
            std::sort(files.begin(),files.end()); paths.insert(paths.end(),files.begin(),files.end());
        } else paths.push_back(fs::canonical(input));
    }
    if (paths.empty()) throw std::runtime_error("No .bin event files found");
    const auto rules=readRules(options.parameters);
    std::vector<Source> sources;
    std::set<fs::path> seenPaths; std::set<uint32_t> modules, daqIds;
    uint32_t run=0,clock=0; bool simulated=false;
    for (const auto &p:paths) {
        if (!seenPaths.insert(fs::canonical(p)).second) throw std::runtime_error("Duplicate input file");
        Reader reader(p.string()); Source s; s.path=p; s.header=reader.header();
        if(s.header.module>65535) throw std::runtime_error("Module index does not fit compatible mod/s branch");
        if (!modules.insert(s.header.module).second) throw std::runtime_error("Duplicate module index; do not mix runs or epochs");
        if (!daqIds.insert(s.header.daqId).second) throw std::runtime_error("Duplicate DAQ ID");
        if (sources.empty()) {run=s.header.runId; clock=s.header.clockHz; simulated=s.header.json.at("simulated");}
        else if (run!=s.header.runId || clock!=s.header.clockHz || simulated!=s.header.json.at("simulated").get<bool>())
            throw std::runtime_error("Inputs must have the same run ID, clock and simulation mode");
        checkManifest(s,options.allowIncomplete); sources.push_back(std::move(s));
    }
    if(options.runMode && run!=options.requestedRun) throw std::runtime_error("Requested run number differs from file header");
    std::string started;
    for(const auto &s:sources) {
        const auto stamp=s.manifest.is_null()?std::string():s.manifest.value("started_utc",std::string());
        if(!stamp.empty()) {
            if(!started.empty() && started!=stamp) throw std::runtime_error("Inputs are from different acquisition sessions (started_utc)");
            started=stamp;
        }
        if(s.path.parent_path()!=sources[0].path.parent_path() &&
           (stamp.empty() || sources[0].manifest.is_null() || sources[0].manifest.value("started_utc",std::string()).empty()))
            throw std::runtime_error("Files from different directories require matching run.json started_utc session identities");
    }
    for(const auto &rule:rules) if(!modules.count(rule.first.first))
        throw std::runtime_error("Parameter table refers to an input module that is absent");
    if (sources.size()>1) for (const auto &s:sources) {
        auto sync=unsignedValue(s.header.json.at("settings").at("sync_config"));
        if (!(sync&1)) throw std::runtime_error("Multiple DAQs require shared external RUN timestamps");
        if ((sync&0x7f)!=unsignedValue(sources[0].header.json.at("settings").at("sync_config")))
            throw std::runtime_error("DAQ synchronization settings differ");
    }
    for (const auto &dir:directories) {
        const auto mp=dir/"run.json";
        if (!fs::exists(mp)) continue;
        auto manifest=readJson(mp);
        auto count=unsignedValue(manifest.at("crate").at("modules"));
        for (uint64_t m=0;m<count;++m) {
            bool found=false;
            for (const auto &s:sources) if(s.path.parent_path()==dir && s.header.module==m) found=true;
            if (!found) throw std::runtime_error("Run directory is missing a DAQ .bin file (even empty DAQs have a header)");
        }
    }
    fs::path output=options.output.empty()?fs::path("run"+std::to_string(run)+".root"):options.output;
    output=fs::absolute(output);
    if (fs::exists(output)) throw std::runtime_error("Output already exists: "+output.string());
    if(options.runMode) fs::create_directories(output.parent_path());
    if (!fs::is_directory(output.parent_path())) throw std::runtime_error("Output parent directory does not exist");
    std::unique_ptr<WorkDirectory> work;
    std::unique_ptr<ExternalSort> external;
    if(options.sortMode=="external") {
        work=std::make_unique<WorkDirectory>(options.temp.empty()?output.parent_path():options.temp);
        external=std::make_unique<ExternalSort>(work->path,options.memoryMiB*size_t(1024*1024));
    }
    // Output staging stays on the output filesystem for atomic publication.
    WorkDirectory staging(output.parent_path());
    // Fail early on filesystems without hard links, before decoding a long run.
    { const auto a=staging.path/"publish-probe", b=staging.path/"publish-link";
      std::ofstream probe(a); if(!probe) throw std::runtime_error("Cannot write output filesystem"); probe.close();
      std::error_code ec; fs::create_hard_link(a,b,ec);
      if(ec) throw std::runtime_error("Output filesystem must support hard links (use ext4/xfs/NTFS): "+ec.message());
      fs::remove(a); fs::remove(b); }
    const auto staged=staging.path/"result.root";
    const int compressionSettings=options.compression=="lz4"
        ?ROOT::RCompressionSetting::EDefaults::kUseAnalysis:104;
    std::unique_ptr<TFile> file(TFile::Open(staged.string().c_str(),"CREATE","R5560 decoded events",compressionSettings));
    if (!file || file->IsZombie()) throw std::runtime_error("Cannot create ROOT file");
    TTree tree("tree","R5560 single events (timestamp sorted unless metadata says otherwise)");
    tree.SetAutoFlush(-16*1024*1024); TTree::SetMaxTreeSize(std::numeric_limits<Long64_t>::max());
    UShort_t mod=0,ch=0,sr=125,fw=4,energyxia=0;
    UInt_t daq=0,runid=run,blA=0,blB=0,sl1=0,sg=0,sl2=0;
    ULong64_t ts=0,prev=0,record=0;
    Long64_t tsns=0;
    Double_t energy=0,baseline=0;
    tree.Branch("mod",&mod,"mod/s"); tree.Branch("ch",&ch,"ch/s");
    tree.Branch("sr",&sr,"sr/s"); tree.Branch("fw",&fw,"fw/s");
    tree.Branch("energyxia",&energyxia,"energyxia/s");
    tree.Branch("daq_id",&daq,"daq_id/i"); tree.Branch("run",&runid,"run/i");
    tree.Branch("timestamp",&ts,"timestamp/l"); tree.Branch("previous",&prev,"previous/l");
    tree.Branch("ts",&tsns,"ts/L"); tree.Branch("record",&record,"record/l");
    tree.Branch("energy",&energy,"energy/D"); tree.Branch("baseline",&baseline,"baseline/D");
    tree.Branch("baselineA",&blA,"baselineA/i"); tree.Branch("baselineB",&blB,"baselineB/i");
    tree.Branch("sl1",&sl1,"sl1/i"); tree.Branch("sg",&sg,"sg/i"); tree.Branch("sl2",&sl2,"sl2/i");
    std::map<uint32_t,std::array<std::unique_ptr<TH1D>,32>> hist;
    for (const auto &s:sources) for (unsigned c=0;c<32;++c) {
        auto name="energy_M"+std::to_string(s.header.module)+"_C"+std::to_string(c);
        auto h=std::make_unique<TH1D>(name.c_str(),(name+";Reconstructed energy (uncalibrated);Counts").c_str(),16384,0,16384);
        h->SetDirectory(nullptr); hist[s.header.module][c]=std::move(h);
    }
    uint64_t total=0, decoded=0, filtered=0;
    auto emit=[&](const Event &e) {
        mod=static_cast<UShort_t>(e.module);ch=static_cast<UShort_t>(e.channel);daq=e.daqId;runid=e.runId;
        ts=e.timestamp;prev=e.previous;tsns=e.timeNs;record=e.record;
        energy=e.energy;baseline=e.baseline;blA=e.baselineA;blB=e.baselineB;sl1=e.sl1;sg=e.sg;sl2=e.sl2;
        energyxia=energy<0?0:(energy>=65536?65535:static_cast<UShort_t>(energy));
        if (tree.Fill()<0 || file->TestBit(TFile::kWriteError)) throw std::runtime_error("ROOT event write failed");
        hist[mod][ch]->Fill(energy); ++total;
    };
    json metadata={{"schema","r5560-root-v1"},{"sorted",options.sort},{"run_id",run},
                   {"sort_mode",options.sortMode},
                   {"compression",options.compression},{"compression_level",4},
                   {"clock_hz",clock},{"timestamp_unit","8 ns ticks"},{"ts_unit","ns"},
                   {"energy_algorithm","GUI5560 EventDecoder UserDPP"},
                   {"sort_key",json::array({"ts","mod","record"})},
                   {"reference","wuhongyi/PKUCAENDAQ DecodeAndSortAll bbd4542265a20cb12713c2dda2a1292ad44441d2"},
                   {"channel_rules",json::array()},
                   {"allow_incomplete",options.allowIncomplete},{"sources",json::array()}};
    for(const auto &r:rules) metadata["channel_rules"].push_back({{"mod",r.first.first},{"ch",r.first.second},
        {"offset_ns",r.second.offset},{"energy_low",r.second.low},{"energy_high",r.second.high}});
    // Keep one reader and one lookahead per DAQ; each reader still computes
    // baseline/energy strictly in original file order, before cuts and sorting.
    std::vector<std::unique_ptr<Reader>> readers;
    std::vector<std::array<std::optional<ChannelRule>,32>> fastRules(sources.size());
    for(size_t i=0;i<sources.size();++i) {
        const auto &s=sources[i];
        std::cerr<<"Decoding "<<s.path<<" (Mod "<<s.header.module<<", "<<s.header.payloadBytes<<" event bytes)\n";
        readers.emplace_back(std::make_unique<Reader>(s.path.string()));
        if(readers.back()->header().json!=s.header.json || readers.back()->header().payloadBytes!=s.header.payloadBytes)
            throw std::runtime_error("Input changed after initial inspection");
        for(unsigned ch=0;ch<32;++ch) {
            const auto it=rules.find({s.header.module,ch});
            if(it!=rules.end()) fastRules[i][ch]=it->second;
        }
    }
    auto nextSelected=[&](size_t i,Event &e) {
        while(readers[i]->next(e)) {
            ++sources[i].events; ++decoded;
            const auto &rule=fastRules[i][e.channel];
            if(rule) {
                if(e.energy<rule->low || e.energy>rule->high) {++filtered;continue;}
                if(rule->offset>0 && e.timeNs>std::numeric_limits<int64_t>::max()-rule->offset)
                    throw std::runtime_error("Timestamp offset overflow");
                e.timeNs+=rule->offset;
            }
            return true;
        }
        return false;
    };
    if(options.sortMode=="window") {
        std::cerr<<"Rolling window: "<<options.windowNs<<" ns, write "<<options.writeFraction*100
                 <<"%, retain "<<(1-options.writeFraction)*100<<"%; no intermediate sort files\n";
        WindowSort rolling(options.memoryMiB*size_t(1024*1024),options.writeFraction);
        std::vector<Event> next(sources.size());
        std::vector<bool> have(sources.size());
        for(size_t i=0;i<sources.size();++i) have[i]=nextSelected(i,next[i]);
        auto progress=std::chrono::steady_clock::now();
        for(;;) {
            bool any=false; int64_t earliest=std::numeric_limits<int64_t>::max();
            for(size_t i=0;i<sources.size();++i) if(have[i]) {any=true;earliest=std::min(earliest,next[i].timeNs);}
            if(!any) {rolling.finish(emit);break;}
            // Jump over empty time ranges instead of iterating billions of empty windows.
            const auto end=earliest>std::numeric_limits<int64_t>::max()-options.windowNs
                ?std::numeric_limits<int64_t>::max():earliest+options.windowNs;
            for(size_t i=0;i<sources.size();++i) {
                while(have[i] && (next[i].timeNs<end || end==std::numeric_limits<int64_t>::max())) {
                    rolling.add(next[i]);
                    have[i]=nextSelected(i,next[i]);
                }
            }
            rolling.flush(emit);
            const auto now=std::chrono::steady_clock::now();
            if(now-progress>=std::chrono::seconds(5)) {
                std::cerr<<"Decoded "<<decoded<<", written "<<total<<", retained "<<rolling.size()<<" events\n";
                progress=now;
            }
        }
        metadata["rolling_sort"]={{"window_ns",std::to_string(options.windowNs)},
            {"write_fraction",options.writeFraction},{"peak_events",std::to_string(rolling.peakEvents())},
            {"cycles",std::to_string(rolling.cycles())},{"late_event_policy","fail; do not publish output"}};
    } else {
        for(size_t i=0;i<sources.size();++i) {
            Event e{};
            while(nextSelected(i,e)) {if(external) external->add(e); else emit(e);}
        }
        if(external) {std::cerr<<"Sorting and writing ROOT events...\n";external->finish(emit);}
    }
    for(size_t i=0;i<sources.size();++i) {
        auto &s=sources[i];
        s.counts=readers[i]->counts(); checkCounts(s);
        metadata["sources"].push_back({{"file",s.path.filename().string()},{"header",s.header.json},
            {"events",std::to_string(s.events)},{"payload_bytes",std::to_string(s.header.payloadBytes)},
            {"channel_counts",s.counts},{"run_manifest",s.manifest}});
    }
    metadata["events"]=std::to_string(total);
    metadata["decoded_events"]=std::to_string(decoded);
    metadata["filtered_events"]=std::to_string(filtered);
    file->cd();
    if(tree.Write()<=0) throw std::runtime_error("ROOT tree write failed");
    for(auto &module:hist) for(auto &h:module.second) if(h->Write()<=0) throw std::runtime_error("Histogram write failed");
    TObjString meta(metadata.dump().c_str());
    if(meta.Write("r5560_metadata")<=0) throw std::runtime_error("ROOT metadata write failed");
    tree.SetDirectory(nullptr); file->Close();
    if(file->TestBit(TFile::kWriteError)) throw std::runtime_error("ROOT close failed");
    // Hard-link publication is atomic and refuses replacement on POSIX and NTFS.
    fs::create_hard_link(staged,output);
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-conversionStart).count();
    std::cout<<"Wrote "<<total<<" events to "<<output<<" in "<<seconds<<" s ("
             <<decoded/seconds<<" decoded events/s)\n";
    return 0;
}
int main(int argc,char **argv) {
    try {return convert(parse(argc,argv));}
    catch(const std::exception &e) {std::cerr<<"DecodeAndSortAll: "<<e.what()<<"\n";return 1;}
}
