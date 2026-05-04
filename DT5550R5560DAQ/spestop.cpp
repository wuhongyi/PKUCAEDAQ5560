// spestop.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 19:52:50 2021 (+0800)
// Last-Updated: 三 12月 29 20:25:05 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 3
// URL: http://wuhongyi.cn 

#include "Global.h"

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
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
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

  // stop
#ifdef DT5550 
  ret = NI_USB3_WriteReg(0,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(0,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif
  if(ret!=0) printf("SCI_REG_Spectrum_STOP: %d\n",ret);
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  NI_USB3_CloseConnection(&handle);
#endif 
  printf("\n");
  
  
  return 0;
}  

// 
// spestop.cpp ends here
