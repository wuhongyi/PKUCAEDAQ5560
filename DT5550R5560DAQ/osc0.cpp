// osc0.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 16:05:25 2021 (+0800)
// Last-Updated: 六 1月 29 14:50:52 2022 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 14
// URL: http://wuhongyi.cn 

#include "ana.hh"
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
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
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
  uint32_t rc;
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
  
  int loop = 0;
  
  uint32_t data_osc[2048];
  uint32_t read_data_osc;
  uint32_t valid_data_osc;
  uint32_t status_osc = 0;
  uint32_t position = 0;
  uint32_t size_osc = 2048;
  uint32_t read_analog[2048];
  uint32_t read_digital0[2048];
  uint32_t read_digital1[2048];
  uint32_t read_digital2[2048];
  uint32_t read_digital3[2048];
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
  
  uint32_t pre_trigger = wuReadData::ReadValue<uint32_t>("OSC0TriggerPre","pars.txt");
  uint32_t triggermode = wuReadData::ReadValue<uint32_t>("OSC0TriggerMode32CH","pars.txt");
  uint32_t triggerch = wuReadData::ReadValue<uint32_t>("OSC0TriggerChannel","pars.txt");

  uint32_t configtriggermode;
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  // 设置输出数据的抽取因子
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR, &handle);
  if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // 设置触发之前保留的采样点数
  ret = NI_USB3_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER, &handle);
  if(ret!=0) printf("PRETRIGGER: %d\n",ret);
#endif

#ifdef R5560
  // 设置输出数据的抽取因子
  ret = NI_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR, &handle);
  if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // 设置触发之前保留的采样点数
  ret = NI_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER, &handle);
  if(ret!=0) printf("PRETRIGGER: %d\n",ret);
#endif

  // 设置触发
  // [2:0] 触发模式
  // 000 -> 外部触发
  // 010 -> 软件触发
  // 001 -> 模拟信号触发
  // 100 -> 逻辑通道 0 触发
  // 101 -> 逻辑通道 1 触发
  // 110 -> 逻辑通道 2 触发
  // 111 -> 逻辑通道 3 触发
  // [3] 当采用模拟信号触发时，0/1上升/下降沿触发
  // [7] 软件触发的0->1
  // [15:8] 触发channel

  if(triggermode == 2)
    configtriggermode = (triggerch << 8) + 0x82;
  else if(triggermode == 4)
    configtriggermode = (triggerch << 8) + 0x4;
  else
    {
      printf("CONFIG TRIGGER MODE ERROR ...\n");
      return 1;
    }
#ifdef DT5550
  ret = NI_USB3_WriteReg(configtriggermode, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(configtriggermode, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE, &handle);
#endif  
  if(ret!=0) printf("TRIGGER_MODE: %d\n",ret);
  
  // 模拟通道触发的阈值
#ifdef DT5550
  ret = NI_USB3_WriteReg(600, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_LEVEL, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(600, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_LEVEL, &handle);
#endif  
  if(ret!=0) printf("TRIGGER_LEVEL: %d\n",ret);
  
  // 由0->1时测量
#ifdef DT5550
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM, &handle);
#endif 
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
#ifdef DT5550
  ret = NI_USB3_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM, &handle);
#endif 
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
  
  // READ_STATUS [0]=1 表示可以读取数据

  while (status_osc != 1)
    {
#ifdef DT5550
      ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS, &handle);
#endif 
      if(ret!=0) printf("READ_STATUS: %d\n",ret);
      printf("loop  status_osc  %d %8X\n",loop, status_osc);

      if(ret!=0) {printf("loop error\n");   ;break;}
      loop++;
      usleep(1000000);
      if(loop>10) exit(0);
    }

#ifdef DT5550
  ret = NI_USB3_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION, &handle);
#endif
#ifdef R5560
  ret = NI_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION, &handle);
#endif 
  
  if(ret!=0) printf("READ_POSITION: %d\n",ret);
  printf("position   %d\n",position);


  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
  

  TGraph *gg[32];

  for (int i = 0; i < 32; ++i)
    {
      gg[i] = new TGraph();

      // SCI_REG_OScilloscope_0_FIFOADDRESS 地址起点，16bit，低位为数据，高位为ch。
      // 当前2048/32。[15：11]为通道数。0x800 * ch
#ifdef DT5550
      ret = NI_USB3_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i,REG_ACCESS,1000, &handle, &read_data_osc, &valid_data_osc);
#endif
#ifdef R5560
      ret = NI_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i, &handle, &read_data_osc);
#endif 
      
      if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;

      // ret = NI_USB3_ReadData(data_osc,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;
      // ret = NI_USB3_ReadData(data_osc+size_osc/2,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i+size_osc/2,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;

      
      ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
      if(ret!=0) printf("RECONSTRUCT: %d\n",ret);
		

      for (uint32_t j = 0; j < size_osc-2; ++j)
	{
	  // gg->SetPoint(i,i,data_osc[i]);
	  gg[i]->SetPoint(j,double(j),double(read_analog[j]));
	
	  // std::cout<<i <<"  "<<read_analog[i]<<std::endl;
	}
      // if(i==0) break;
    }


  TCanvas *c1 = new TCanvas("c1","c1");
  if(argc == 1)
    {
      c1->Divide(8,4);
      // c1->Divide(3,1);
      for (int i = 0; i < 32; ++i)
	{
	  c1->cd(i+1);
	  gg[i]->Draw("AP*");
	}
    }
  else if(argc == 2)
    {
      int ch = atoi(argv[1]);
      if(ch<0 || ch>31)
	{
	  std::cout<<"./osc0 [ch]"<<std::endl;
	  exit(1);
	}
      c1->cd();
      gg[ch]->Draw("AP*");
    }
    
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
// osc0.cpp ends here
