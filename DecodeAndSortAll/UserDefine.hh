#pragma once
// Run-number mode: ./decodeandsort 70
// Paths are relative to the process working directory, as in PKUCAENDAQ.
// GUI5560 run directories are unpadded: RAWFILEPATH/70/*.bin.
#define RAWFILEPATH "../data/"
#define ROOTFILEPATH "../rootfile/"
#define ROOTFILENAME "data"



// Optional five-column timing/energy table. Empty means no shifts or cuts.
#define PARAMETERFILE ""


// Rolling time window, matching the reference DecodeAndSortAll workflow.
#define TIMEBUFF 1000000000LL  // ns: read approximately one second from every DAQ
#define PROCESSBUFF 0.9       // write earliest 90%, keep newest 10% for the next pass
// Module count, sampling rate and integration parameters come from each .bin
// header. They must not be copied from a different run's current GUI settings.
