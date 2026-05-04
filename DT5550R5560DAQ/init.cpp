// init.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 五 4月  2 09:47:30 2021 (+0800)
// Last-Updated: 四 12月 23 21:07:30 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 26
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
  bool impedance;
  uint32_t offsettop, offsetbotton;
  uint32_t single_reg;
  int nch = -1;
  std::vector<uint32_t>* vpol = new std::vector<uint32_t>;
  std::vector<uint32_t>* viiralpha = new std::vector<uint32_t>;
  std::vector<uint32_t>* vfilter = new std::vector<uint32_t>;
  
  sn = wuReadData::ReadValue<std::string>("SerialNumber","pars.txt");
  impedance = wuReadData::ReadValue<bool>("Impedance50R","pars.txt");

  offsettop = wuReadData::ReadValue<uint32_t>("OffsetCH16_31","pars.txt");
  offsetbotton = wuReadData::ReadValue<uint32_t>("OffsetCH0_15","pars.txt");

  nch = wuReadData::ReadVector("SignalPolarity","pars.txt",vpol); 
  if(nch != 32) printf("pars SignalPolarity error ...\n");
  
  nch = wuReadData::ReadVector("FILTER","pars.txt",vfilter); 
  if(nch != 32) printf("pars FILTER error ...\n");
  
  nch = wuReadData::ReadVector("IIRALPHA","pars.txt",viiralpha); 
  if(nch != 32) printf("pars IIRALPHA error ...\n");
  
  // std::cout<<sn<<"  "<<impedance<<"  "<<offsettop<<"  "<<offsetbotton<<std::endl;

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550   
  NI_USB3_ListDevices(listofdevices, "DT5550",  &Count);
  printf("Devices found: %d\n%s\n", Count, listofdevices);

  NI_USB3_SetboardModel(DT5550, &handle);
    
  if (NI_USB3_ConnectDevice(const_cast<char*>(sn.c_str()), &handle))
    {
      printf("Unable to connect to the board\n");
      exit(0);
    }

  ret = NI_USB3_SetIICControllerBaseAddress(SCI_REG_I2C_MASTER_CTRL, SCI_REG_I2C_MASTER_MON, &handle);
  if(ret!=0) printf("IIC: %d\n",ret);
  ret = NI_USB3_SetImpedance(impedance,&handle);
  if(ret!=0) printf("SetImpedance: %d\n",ret);
  ret = NI_USB3_SetOffset(true,offsettop,&handle);
  if(ret!=0) printf("SetOffset: %d\n",ret);
  ret = NI_USB3_SetOffset(false,offsetbotton,&handle);
  if(ret!=0) printf("SetOffset: %d\n",ret);

#endif

#ifdef R5560
  err_code = R5560_ConnectTCP(IPADDR, 8888, &handle);
  if (err_code != 0)
    {
      printf("Unable to connect to: %s - %d\n", IPADDR, err_code);
      return 0;
    }
#endif  

  
  usleep(1000000);

  for (int i = 0; i < 32; ++i)
    {
#ifdef DT5550     
      ret = NI_USB3_WriteReg(vpol->at(i), SCI_REG_POL_START+i, &handle);
      if(ret!=0) printf("WRITE_REG_POL: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_POL_START+i, &handle);
      if(ret!=0) printf("READ_REG_POL: %d\n",ret);
      if(vpol->at(i) != single_reg) printf("WRITE_REG_POL_ERROR. CH: %d  %d  %d\n",i,vpol->at(i),single_reg);

      ret = NI_USB3_WriteReg(vfilter->at(i), SCI_REG_FILTER_START+i, &handle);
      if(ret!=0) printf("WRITE_FILTER_POL: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_FILTER_START+i, &handle);
      if(ret!=0) printf("READ_REG_POL: %d\n",ret);
      if(vfilter->at(i) != single_reg) printf("WRITE_REG_FILTER_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(viiralpha->at(i), REG_IIRALPHA_START+i, &handle);
      if(ret!=0) printf("WRITE_REG_IIRALPHA: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, REG_IIRALPHA_START+i, &handle);
      if(ret!=0) printf("READ_REG_IIRALPHA: %d\n",ret);
      if(viiralpha->at(i) != single_reg) printf("WRITE_REG_IIRALPHA_ERROR. CH: %d\n",i);
#endif
#ifdef R5560
      ret = NI_WriteReg(vpol->at(i), SCI_REG_POL_START+i, &handle);
      if(ret!=0) printf("WRITE_REG_POL: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_POL_START+i, &handle);
      if(ret!=0) printf("READ_REG_POL: %d\n",ret);
      if(vpol->at(i) != single_reg) printf("WRITE_REG_POL_ERROR. CH: %d  %d  %d\n",i,vpol->at(i),single_reg);

      ret = NI_WriteReg(vfilter->at(i), SCI_REG_FILTER_START+i, &handle);
      if(ret!=0) printf("WRITE_FILTER_POL: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_FILTER_START+i, &handle);
      if(ret!=0) printf("READ_REG_POL: %d\n",ret);
      if(vfilter->at(i) != single_reg) printf("WRITE_REG_FILTER_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(viiralpha->at(i), REG_IIRALPHA_START+i, &handle);
      if(ret!=0) printf("WRITE_REG_IIRALPHA: %d\n",ret);
      ret = NI_ReadReg(&single_reg, REG_IIRALPHA_START+i, &handle);
      if(ret!=0) printf("READ_REG_IIRALPHA: %d\n",ret);
      if(viiralpha->at(i) != single_reg) printf("WRITE_REG_IIRALPHA_ERROR. CH: %d\n",i);
#endif        
    }


  usleep(10);
#ifdef DT5550
  ret = NI_USB3_WriteReg(1, REG_IIRRESET_SINGLE, &handle);
  if(ret!=0) printf("WRITE_REG_IIRRESE: %d\n",ret);
  usleep(10);
  ret = NI_USB3_WriteReg(0, REG_IIRRESET_SINGLE, &handle);
  if(ret!=0) printf("WRITE_REG_IIRRESE: %d\n",ret);
#endif
#ifdef R5560
  ret = NI_WriteReg(1, REG_IIRRESET_SINGLE, &handle);
  if(ret!=0) printf("WRITE_REG_IIRRESE: %d\n",ret);
  usleep(10);
  ret = NI_WriteReg(0, REG_IIRRESET_SINGLE, &handle);
  if(ret!=0) printf("WRITE_REG_IIRRESE: %d\n",ret);
#endif   
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  NI_USB3_CloseConnection(&handle);
#endif
  printf("\n");
  
  
  return 0;
}

// 
// init.cpp ends here
