// osc1.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 17:10:53 2021 (+0800)
// Last-Updated: 三 10月 13 19:49:23 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 16
// URL: http://wuhongyi.cn 

#include "ana.hh"

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
#include "ni_usb3_wrapper.h"
#include "RegisterFile.h"
#include "wuReadData.hh"
#include <iostream>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

// int OSCILLOSCOPE_RECONSTRUCT(uint32_t *data_osc, uint32_t position, uint32_t pre_trigger, uint32_t n_samples, uint32_t *read_analog, uint32_t *read_digital0, uint32_t *read_digital1, uint32_t *read_digital2, uint32_t *read_digital3)
// {
//   int n_ch = 1;
//   // int n_samples = 2048;
//   if (position > n_samples) return -1;
//   for(int n=0; n< n_ch; n++)
//     {
//       int current = position - pre_trigger-1;
//       if (current > 0)
// 	{
// 	  int k = 0;
// 	  for (int i = current; i < n_samples; i++)
// 	    {
// 	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
// 	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
// 	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
// 	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
// 	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
// 	      k++;
// 	    }
// 	  for (int i = 0; i < current; i++)
// 	    {
// 	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
// 	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
// 	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
// 	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
// 	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
// 	      k++;
// 	    }
// 	}
//       else
// 	{
// 	  int k = 0;
// 	  for (int i = n_samples+current; i < n_samples; i++)
// 	    {
// 	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
// 	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
// 	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
// 	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
// 	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
// 	      k++;
// 	    }
// 	  for (int i = 0; i < n_samples + current; i++)
// 	    {
// 	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
// 	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
// 	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
// 	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
// 	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
// 	      k++;
// 	    }
// 	}
//     }
//   return 0;
// }


int main(int argc, char *argv[])
{
  TRint *theApp = new TRint("Rint", &argc, argv);
  
  char listofdevices[1024];
  int Count;
  NI_HANDLE dt5550;
  int ret;

  std::string sn;
  sn = wuReadData::ReadValue<std::string>("SerialNumber","pars.txt");

  NI_USB3_ListDevices(listofdevices, "DT5550",  &Count);
  printf("Devices found: %d\n%s\n", Count, listofdevices);

  NI_USB3_SetboardModel(DT5550, &dt5550);
  
  if (NI_USB3_ConnectDevice(const_cast<char*>(sn.c_str()), &dt5550))
    {
      printf("Unable to connect to the board\n");
      exit(0);
    }

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

  uint32_t triggerch = wuReadData::ReadValue<uint32_t>("OSC1TriggerChannel","pars.txt");
  uint32_t pre_trigger = wuReadData::ReadValue<uint32_t>("OSC1TriggerPre","pars.txt");
  uint32_t configtriggermode;
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  ret = NI_USB3_WriteReg(triggerch, REG_OSC1CH_SINGLE, &dt5550);
  if(ret!=0) printf("OSC1CH_SINGLE: %d\n",ret);
  usleep(1000000);
  
  // 设置输出数据的抽取因子
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR+0x10000, &dt5550);
  if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // 设置触发之前保留的采样点数
  ret = NI_USB3_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER+0x10000, &dt5550);
  if(ret!=0) printf("PRETRIGGER: %d\n",ret);

  configtriggermode = 0x4;
  ret = NI_USB3_WriteReg(configtriggermode, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE+0x10000, &dt5550);
  if(ret!=0) printf("TRIGGER_MODE: %d\n",ret);  


  // 由0->1时测量
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &dt5550);
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
  ret = NI_USB3_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM+0x10000, &dt5550);
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
  
  while (status_osc != 1)
    {
      ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS+0x10000, &dt5550);
      if(ret!=0) printf("READ_STATUS: %d\n",ret);
      printf("loop  status_osc  %d %8X\n",loop, status_osc);

      if(ret!=0) {printf("loop error\n");   ;break;}
      loop++;
      usleep(1000000);
      if(loop>10) exit(0);
    }

  ret = NI_USB3_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION+0x10000, &dt5550);
  if(ret!=0) printf("READ_POSITION: %d\n",ret);

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
  
  TCanvas *c1 = new TCanvas("c1","c1");
  c1->Divide(1,5);
  TGraph *gg[4];
  TGraph *ggg[4];

  for (int i = 0; i < 4; ++i)
    {
      gg[i] = new TGraph();
      if(i == 0)
	{
	  ggg[0] = new TGraph();
	  ggg[1] = new TGraph();
	  ggg[2] = new TGraph();
	  ggg[3] = new TGraph();
	}

      ret = NI_USB3_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x10000+0x800*i,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      if(ret!=0) printf("FIFOADDRESS: %d\n",ret);

      ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
      if(ret!=0) printf("RECONSTRUCT: %d\n",ret);


      for (uint32_t j = 0; j < size_osc; ++j)
	{
	  // gg->SetPoint(i,i,data_osc[i]);
	  double tmp = read_analog[j];
	  if(i==1)
	    {
	      if((read_analog[j]&0x00008000)==0x00008000)
		{
		  tmp = 65535-read_analog[j];
		}
	      else
		{
		  tmp = read_analog[j];
		}
	    }
	  gg[i]->SetPoint(j,double(j),tmp);

	  if(i == 0)
	    {
	      ggg[0]->SetPoint(j,double(j),double(read_digital0[j]));
	      ggg[1]->SetPoint(j,double(j),double(read_digital1[j]));
	      ggg[2]->SetPoint(j,double(j),double(read_digital2[j]));
	      ggg[3]->SetPoint(j,double(j),double(read_digital3[j]));
	    }
	  
	  // std::cout<<i <<"  "<<read_analog[i]<<std::endl;
	}
      // gg[i]->Draw("AP*");
    }

  // 1黑 2红 3绿 4蓝 5黄 6紫
  
  c1->cd(1);
  gg[0]->SetLineColor(1);
  gg[0]->Draw("AL");
  c1->cd(2);
  gg[1]->SetLineColor(4);
  gg[1]->Draw("AL");
  c1->cd(3);
  gg[2]->SetLineColor(2);
  gg[2]->Draw("AL");
  c1->cd(4);
  gg[3]->SetLineColor(3);
  gg[3]->Draw("AL");

  c1->cd(5);
  ggg[0]->SetLineColor(4);
  ggg[0]->Draw("AL");
  ggg[1]->SetLineColor(3);
  ggg[1]->Draw("Lsame");
  ggg[2]->SetLineColor(6);
  ggg[2]->Draw("Lsame");
  ggg[3]->SetLineColor(2);
  ggg[3]->Draw("Lsame");
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  NI_USB3_CloseConnection(&dt5550);
  printf("\n");


  theApp->Run();
  delete theApp;  
  
  return 0;
}
  

// 
// osc1.cpp ends here
