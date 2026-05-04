// speshow.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 19:56:10 2021 (+0800)
// Last-Updated: 三 12月 29 20:29:10 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 7
// URL: http://wuhongyi.cn 

#include "Global.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TRint.h"
#include "TH1.h"

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef DT5550
#include "ni_usb3_wrapper.h"
#endif  
#ifdef R5560
#include "R5560_SDKLib.h"
#endif  
#include "RegisterFile.h"
#include "wuReadData.hh"
#include <iostream>
#include <cstdlib>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
  if(argc < 2)
    {
      std::cout<< "argc:" << argc <<std::endl;
      exit(1);
    }
  
   int ch = atoi(argv[1]);
   if(ch<0 || ch>31)
     {
       std::cout<<"./speshow [ch]"<<std::endl;
       exit(1);
     }
  

  
  TRint *theApp = new TRint("Rint", &argc, argv);

  char listofdevices[1024];
  int Count;  
#ifdef DT5550
  NI_HANDLE handle;
#endif
#ifdef R5560
  tR5560_Handle handle;
#endif 
  int ret;
  int err_code = 0;
  std::string sn;
  sn = wuReadData::ReadValue<std::string>("SerialNumber","pars.txt");

#ifdef DT5550
  NI_USB3_ListDevices(listofdevices, "DT5550",  &Count);
  printf("Devices found: %d\n%s\n", Count, listofdevices);
  NI_USB3_SetboardModel(DT5550, &handle);
  if (NI_USB3_ConnectDevice(const_cast<char*>(sn.c_str()), &handle))
    {
      printf("Unable to connect to the board\n");
      exit(0);
    }
#endif

#ifdef R5560
  err_code = R5560_ConnectTCP(IPADDR, 8888, &handle);
  if (err_code != 0)
    {
      printf("Unable to connect to: %s - %d\n", IPADDR, err_code);
      return 0;
    }
#endif
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  uint32_t status_spectrum= 0;

  uint32_t bins = 8192;
  uint32_t data_spectrum[16384];
  uint32_t read_data_spectrum;
  uint32_t valid_data_spectrum;
  int32_t timeout_spectrum = 1000;
  uint32_t spectrum[16384];

  TCanvas *c1 = new TCanvas("c1","c1");
  TH1I *hspe = new TH1I("hspe","",int(bins),double(0),double(bins));

#ifdef DT5550
  ret = NI_USB3_ReadData(data_spectrum, bins, SCI_REG_Spectrum_0_FIFOADDRESS+0x10000*ch, REG_ACCESS, timeout_spectrum, &handle, &read_data_spectrum, &valid_data_spectrum);
#endif
#ifdef R5560
  ret = NI_ReadData(data_spectrum, bins, SCI_REG_Spectrum_0_FIFOADDRESS+0x10000*ch, &handle, &read_data_spectrum);
#endif
  if(ret!=0) printf("READ_Spectrum_ERROR: %d\n",ret);
  for (int i = 0; i < bins; i++)
    {
      spectrum[i] = data_spectrum[i];
      hspe->SetBinContent(i+1,double(spectrum[i]));
    }
      
  c1->cd();
  hspe->Draw("hist");


  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  NI_USB3_CloseConnection(&handle);
#endif
  printf("\n");
  
  theApp->Run();
  delete theApp;
  return 0;
}  

// 
// speshow.cpp ends here
