// spestart.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 19:47:29 2021 (+0800)
// Last-Updated: 三 12月 29 20:25:03 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 4
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

  int32_t Rebin = 0;
  int32_t LimitMode = 0;
  int32_t LimitValue = 0;
  
  // set pars
#ifdef DT5550
  ret = NI_USB3_WriteReg(Rebin, SCI_REG_Spectrum_0_CONFIG_REBIN, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(Rebin, SCI_REG_Spectrum_0_CONFIG_REBIN, &handle);
#endif  
  if(ret!=0) printf("SCI_REG_Spectrum_0_CONFIG_REBIN: %d\n",ret);
  int32_t limit = 0;
  limit = (1 << (LimitMode + 29)) + LimitValue;
#ifdef DT5550 
  ret = NI_USB3_WriteReg(limit, SCI_REG_Spectrum_0_CONFIG_LIMIT, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(limit, SCI_REG_Spectrum_0_CONFIG_LIMIT, &handle);
#endif
  if(ret!=0) printf("SCI_REG_Spectrum_0_CONFIG_LIMIT: %d\n",ret);

  // flush
#ifdef DT5550  
  ret = NI_USB3_WriteReg(1,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(1,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif  
  if(ret!=0) printf("SCI_REG_Spectrum_FLUSH: %d\n",ret);
  
  // reset
#ifdef DT5550 
  ret = NI_USB3_WriteReg(2,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(2,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif   
  if(ret!=0) printf("SCI_REG_Spectrum_RESET: %d\n",ret);
  
  // start
#ifdef DT5550
  ret = NI_USB3_WriteReg(4,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(4,SCI_REG_Spectrum_0_CONFIG, &handle);
#endif  
  if(ret!=0) printf("SCI_REG_Spectrum_START: %d\n",ret);

  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  NI_USB3_CloseConnection(&handle);
#endif 
  printf("\n");
  
  
  return 0;
}  

// 
// spestart.cpp ends here
