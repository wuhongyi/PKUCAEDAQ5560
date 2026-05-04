// osc2.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 日 4月 18 18:37:22 2021 (+0800)
// Last-Updated: 六 1月 29 15:39:53 2022 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 44
// URL: http://wuhongyi.cn 

#include "Global.h"
#include "ana.hh"

#include "TCanvas.h"
#include "TGraph.h"
#include "TRint.h"
#include "TH1.h"
#include "TFile.h"
#include "TTree.h"

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
  int err_code = 0;
  std::string sn;
  
  sn = wuReadData::ReadValue<std::string>("SerialNumber","pars.txt");
  
#ifdef DT5550
  NI_USB3_ListDevices(listofdevices, "DT5550",  &Count);
  printf("Devices found: %d\n%s\n", Count, listofdevices);

  NI_USB3_SetboardModel(DT5550, &handle);
  
  if (NI_USB3_ConnectDevice(const_cast<char*>(sn.c_str()), &dt5550))
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
  const int N = 32768;
  
  uint32_t data_osc[N];
  uint32_t read_data_osc;
  uint32_t valid_data_osc;
  uint32_t status_osc = 0;
  uint32_t position = 0;
  uint32_t size_osc = N;
  uint32_t read_analog[N];
  uint32_t read_digital0[N];
  uint32_t read_digital1[N];
  uint32_t read_digital2[N];
  uint32_t read_digital3[N];
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  uint32_t triggerch = wuReadData::ReadValue<uint32_t>("OSC1TriggerChannel","pars.txt");
  uint32_t pre_trigger = wuReadData::ReadValue<uint32_t>("OSC1TriggerPre","pars.txt");
  uint32_t configtriggermode;
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  ret = NI_USB3_WriteReg(triggerch, REG_OSC1CH_SINGLE, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(triggerch, REG_OSC1CH_SINGLE, &handle);
#endif  
  if(ret!=0) printf("OSC1CH_SINGLE: %d\n",ret);
  usleep(1000000);
  
  // 设置输出数据的抽取因子
#ifdef DT5550
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR+0x10000, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR+0x10000, &handle);
#endif  
  if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // 设置触发之前保留的采样点数
#ifdef DT5550
  ret = NI_USB3_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER+0x10000, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER+0x10000, &handle);
#endif  
  if(ret!=0) printf("PRETRIGGER: %d\n",ret);

  configtriggermode = 0x4;
#ifdef DT5550
  ret = NI_USB3_WriteReg(configtriggermode, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE+0x10000, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(configtriggermode, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE+0x10000, &handle);
#endif  
  if(ret!=0) printf("TRIGGER_MODE: %d\n",ret);

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef SAVEROOTDATA

  TFile *filew = new TFile("osc2.root","RECREATE");//"RECREATE" "READ"
  if(!filew->IsOpen())
    {
      std::cout<<"Can't open root file"<<std::endl;
    }

  TTree *tree = new TTree("tree","Hongyi Wu Data");

  int pos;
  int len;
  int data[32768];
  int d0[32768];
  tree->Branch("pos",&pos,"pos/I");
  tree->Branch("len",&len,"len/I");
  tree->Branch("data",&data,"data[len]/I");
  tree->Branch("d0",&d0,"d0[len]/I");

  
  int event = 0;
  while(event < EVENTNUN)
    {
      if(event % 100 == 0) std::cout<<"Event: "<<event<<std::endl;
     
      // 由0->1时测量
#ifdef DT5550
      ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif
#ifdef R5560
      ret = NI_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif  
      if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
#ifdef DT5550
      ret = NI_USB3_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif
#ifdef R5560
      ret = NI_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif  
      if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
      // usleep(500);

      loop = 0;
#ifdef DT5550
      ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif  
      if(ret!=0) printf("READ_STATUS: %d\n",ret);
      
      while (status_osc != 1)
	{
#ifdef DT5550
	  ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif
#ifdef R5560
	  ret = NI_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif  
	  if(ret!=0) printf("READ_STATUS: %d\n",ret);
	  if(loop>0 && loop%10000==0)printf("loop  status_osc  %d %8X\n",loop/10000, status_osc);

	  if(ret!=0) {printf("loop error\n");   ;break;}
	  loop++;
	  usleep(100);
	  if(loop>100000) exit(0);
	}

#ifdef DT5550
      ret = NI_USB3_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION+0x10000, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION+0x10000, &handle);
#endif  
      if(ret!=0) printf("READ_POSITION: %d\n",ret);

#ifdef DT5550
      ret = NI_USB3_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x10000,REG_ACCESS,1000, &handle, &read_data_osc, &valid_data_osc);
#endif
#ifdef R5560
      ret = NI_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x10000, &handle, &read_data_osc);
#endif  
      if(ret!=0) printf("FIFOADDRESS: %d\n",ret);

      ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
      if(ret!=0) printf("RECONSTRUCT: %d\n",ret);

      pos = position;
      len = N;
      for (int j = 0; j < N; ++j)
      	{
      	  data[j] = read_analog[j];
	  d0[j] = read_digital0[j];
      	}
      tree->Fill();//loop

      event++;
    }


 filew->cd();
 tree->Write();
 filew->Close();

#else
  
  // 由0->1时测量
#ifdef DT5550
 ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif
#ifdef R5560
 ret = NI_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif  
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
#ifdef DT5550
  ret = NI_USB3_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif
#ifdef R5560
  ret = NI_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &handle);
#endif  
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);

  status_osc = 0;
  while (status_osc != 1)
    {
#ifdef DT5550
      ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif
#ifdef R5560
      ret = NI_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &handle);
#endif  
      if(ret!=0) printf("READ_STATUS: %d\n",ret);
      printf("loop  status_osc  %d %8X\n",loop, status_osc);

      if(ret!=0) {printf("loop error\n");   ;break;}
      loop++;
      usleep(1000000);
      if(loop>10) exit(0);
    }

#ifdef DT5550
  ret = NI_USB3_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION+0x10000, &handle);
#endif
#ifdef R5560
  ret = NI_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION+0x10000, &handle);
#endif  
  if(ret!=0) printf("READ_POSITION: %d\n",ret);

 
  TCanvas *c1 = new TCanvas("c1","c1");
  TGraph *gg;
  gg = new TGraph();

#ifdef DT5550
  ret = NI_USB3_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x10000,REG_ACCESS,1000, &handle, &read_data_osc, &valid_data_osc);
#endif
#ifdef R5560
  ret = NI_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x10000, &handle, &read_data_osc);
#endif  
  if(ret!=0) printf("FIFOADDRESS: %d\n",ret);

  ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
  if(ret!=0) printf("RECONSTRUCT: %d\n",ret);

  for (uint32_t j = 0; j < size_osc-2; ++j)
    {
      gg->SetPoint(j,double(j),double(read_analog[j]));
    }

  c1->cd();
  gg->SetLineColor(1);
  gg->Draw("AP");

#endif
  
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
// osc2.cpp ends here
