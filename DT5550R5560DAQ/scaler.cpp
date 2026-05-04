// scaler.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 7月 24 18:49:04 2021 (+0800)
// Last-Updated: 五 12月 24 16:58:27 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 7
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
  uint32_t single_reg;
  std::string sn;
  int err_code = 0;
  
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

  uint32_t triggercount[32];
  uint32_t triggerrate[32];

  
    for (int i = 0; i < 32; ++i)
    {
#ifdef DT5550
      ret = NI_USB3_ReadReg(&triggercount[i], REG_TRIGGERCOUNT_START+i, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&triggercount[i], REG_TRIGGERCOUNT_START+i, &handle);
#endif
      if(ret != 0) printf("READ_REG_TRIGGERCOUNT: %d\n",ret);
    }
  
    for (int i = 0; i < 32; ++i)
    {
#ifdef DT5550
      ret = NI_USB3_ReadReg(&triggerrate[i], REG_TRIGGERRATE_START+i, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&triggerrate[i], REG_TRIGGERRATE_START+i, &handle);
#endif
      if(ret != 0) printf("READ_REG_TRIGGERRATE: %d\n",ret);
    }

    for (int i = 0; i < 32; ++i)
    {
      printf("%03d  %+20u %+20u \n",i,triggerrate[i],triggercount[i]);
    }

    
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
    
#ifdef DT5550
  NI_USB3_CloseConnection(&handle);
#endif
  printf("\n");
  
  return 0;
}  

// 
// scaler.cpp ends here
