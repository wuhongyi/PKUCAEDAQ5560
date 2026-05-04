#include "TCanvas.h"
#include "TGraph.h"
#include "TRint.h"
#include "TH1.h"

#include <iostream>
#include <fstream>
#include <istream>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <assert.h>

#include <stdlib.h>
#include "ni_usb3_wrapper.h"
#include "RegisterFile.h"

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......


int OSCILLOSCOPE_RECONSTRUCT(uint32_t *data_osc, uint32_t position, int32_t pre_trigger, int32_t n_samples, uint32_t *read_analog, uint32_t *read_digital0, uint32_t *read_digital1, uint32_t *read_digital2, uint32_t *read_digital3)
{
  int n_ch = 1;
  // int n_samples = 2048;
  if (position > n_samples) return -1;
  for(int n=0; n< n_ch; n++)
    {
      int current = position - pre_trigger-1;
      if (current > 0)
	{
	  int k = 0;
	  for (int i = current; i < n_samples; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	  for (int i = 0; i < current; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	}
      else
	{
	  int k = 0;
	  for (int i = n_samples+current; i < n_samples; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	  for (int i = 0; i < n_samples + current; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	}
    }
  return 0;

}


int main(int argc, char *argv[])
{

  TRint *theApp = new TRint("Rint", &argc, argv);


  uint32_t data[16384];
  uint32_t datar[16384];
  uint32_t written_data;
  uint32_t read_data;
  uint32_t valid_data;   
  char board_model[32];   
  float current;
  int SN;
  NI_HANDLE dt5550;
  char listofdevices[1024];
  int Count;


  uint32_t data_osc[16384];
  uint32_t read_data_osc;
  uint32_t valid_data_osc;
  uint32_t status_osc = 0;
  int32_t pre_trigger = 500;
  uint32_t position = 0;
  int32_t size_osc = 16384;
  uint32_t read_analog[16384];
  uint32_t read_digital0[16384];
  uint32_t read_digital1[16384];
  uint32_t read_digital2[16384];
  uint32_t read_digital3[16384];

  uint32_t status_list = 0;
  uint32_t empty_list = 0;
  uint32_t full_list = 0;
  uint32_t word_list = 0;
  uint32_t data_list[8];
  uint32_t read_data_list;
  uint32_t valid_data_list;
  uint32_t size_list = 8;
  int32_t timeout_list = 1000;
  uint32_t ReadListNumber = 0;
  uint32_t TargetDataNumber = 100000;
  uint32_t DownloadDataValues[100000];

  uint32_t single_reg;
  int ret;

  uint32_t regpol[32] = {
			 1,1,1,1,1,1,1,1,
			 1,1,1,1,1,1,1,1,
			 1,1,1,1,1,1,1,1,
			 1,1,1,1,1,1,1,1 };
  uint32_t regthre[32] = {
			  100,100,100,100,100,100,100,100,
			  100,100,100,100,100,100,100,100,
			  100,100,100,100,100,100,100,100,
			  100,100,100,100,100,100,100,100 };

  
  NI_USB3_ListDevices(listofdevices, "DT5550",  &Count);
  printf("Devices found: %d\n%s\n", Count, listofdevices);

  NI_USB3_SetboardModel(DT5550, &dt5550);
    
  if (NI_USB3_ConnectDevice("NI110011", &dt5550)) {
    printf("Unable to connect to the board");
    exit(0);
  }

  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  ret = NI_USB3_ReadReg(&single_reg, 0x00010007, &dt5550);
  if(ret!=0) printf("READ_REG_error: %d\n",ret);
  printf("%d \n",single_reg);
  



  

  // // 设置输出数据的抽取因子
  // ret = NI_USB3_WriteReg(0, 0x10006, &dt5550);
  // if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // // 设置触发之前保留的采样点数
  // ret = NI_USB3_WriteReg(pre_trigger, 0x10003, &dt5550);
  // if(ret!=0) printf("PRETRIGGER: %d\n",ret);
    
  // // 设置触发
  // // [2:0] 触发模式
  // // 000 -> 外部触发
  // // 010 -> 软件触发
  // // 001 -> 模拟信号触发
  // // 100 -> 逻辑通道 0 触发
  // // 101 -> 逻辑通道 1 触发
  // // 110 -> 逻辑通道 2 触发
  // // 111 -> 逻辑通道 3 触发
  // // [3] 当采用模拟信号触发时，0/1上升/下降沿触发
  // // [7] 软件触发的0->1
  // // [15:8] 触发channel
  // ret = NI_USB3_WriteReg(0x82, 0x10002, &dt5550);
  // if(ret!=0) printf("TRIGGER_MODE: %d\n",ret);
  // // 模拟通道触发的阈值
  // ret = NI_USB3_WriteReg(600, 0x10004, &dt5550);
  // if(ret!=0) printf("TRIGGER_LEVEL: %d\n",ret);
  // // 由0->1时测量
  // ret = NI_USB3_WriteReg(0, 0x10005, &dt5550);
  // if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
  // ret = NI_USB3_WriteReg(1, 0x10005, &dt5550);
  // if(ret!=0) printf("CONFIG_ARM: %d\n",ret);


  // // READ_STATUS [0]=1 表示可以读取数据
  // int loop = 0;
  // while (status_osc != 1)
  //   {
  //     ret = NI_USB3_ReadReg(&status_osc, 0x10000, &dt5550);
  //     if(ret!=0) printf("READ_STATUS: %d\n",ret);
  //     printf("loop  status_osc  %d %8X\n",loop, status_osc);

  //     if(ret!=0) {printf("loop error\n");   ;break;}
  //     loop++;
  //     usleep(1000000);
  //     if(loop>10) exit(0);
  //   }


  // ret = NI_USB3_ReadReg(&position, 0x10001, &dt5550);
  // if(ret!=0) printf("READ_POSITION: %d\n",ret);
  // printf("position   %d\n",position);

  
  // TCanvas *c1 = new TCanvas("c1","c1");
  // // c1->Divide(8,4);
  // // c1->Divide(3,1);
  // TGraph *gg[32];

  // for (int i = 0; i < 1; ++i)
  //   {
  //     // c1->cd(i+1);
  //     gg[i] = new TGraph();

  //     // SCI_REG_OScilloscope_0_FIFOADDRESS 地址起点，16bit，低位为数据，高位为ch。
  //     // 当前2048/32。[15：11]为通道数。0x800 * ch
  //     ret = NI_USB3_ReadData(data_osc,size_osc,16384,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
  //     if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
  //     std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;

  //     // ret = NI_USB3_ReadData(data_osc,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
  //     // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
  //     // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;
  //     // ret = NI_USB3_ReadData(data_osc+size_osc/2,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i+size_osc/2,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
  //     // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
  //     // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;


      
  //     ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
  //     if(ret!=0) printf("RECONSTRUCT: %d\n",ret);
		

  //     for (int j = 0; j < size_osc-2; ++j)
  // 	{
  // 	  // gg->SetPoint(i,i,data_osc[i]);
  // 	  gg[i]->SetPoint(j,double(j),double(read_analog[j]));
	
  // 	  // std::cout<<i <<"  "<<read_analog[i]<<std::endl;
  // 	}
  //     gg[i]->Draw("AP*");
  //     // if(i==0) break;
  //   }




  
  // pos 1000 neg 3000
  
  // ret = NI_USB3_SetIICControllerBaseAddress(SCI_REG_I2C_MASTER_CTRL, SCI_REG_I2C_MASTER_MON, &dt5550);
  // if(ret!=0) printf("IIC: %d\n",ret);
  // ret = NI_USB3_SetImpedance(true,&dt5550);
  // if(ret!=0) printf("SetImpedance: %d\n",ret);
  // ret = NI_USB3_SetOffset(true,1000,&dt5550);
  // if(ret!=0) printf("SetOffset: %d\n",ret);
  // ret = NI_USB3_SetOffset(false,1000,&dt5550);
  // if(ret!=0) printf("SetOffset: %d\n",ret);
  // usleep(1000000);

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  // for (int i = 0; i < 32; ++i)
  //   {
  //     ret = NI_USB3_WriteReg(regpol[i], SCI_REG_POL_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_POL: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_POL_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_POL: %d\n",ret);
  //     if(regpol[i] != single_reg) printf("WRITE_REG_POL_ERROR. CH: %d\n",i);

  //     ret = NI_USB3_WriteReg(regthre[i],SCI_REG_THRE_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_THRE: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_THRE_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_THRE: %d\n",ret);
  //     if(regthre[i] != single_reg) printf("WRITE_REG_THRE_ERROR. CH: %d\n",i);

  //   }
  

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  // uint32_t pars;  
  // for (int i = 0; i < 2; ++i)
  //   {
  //     pars = 10;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPTRIGFL_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPTRIGFL: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFL_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPTRIGFL: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPTRIGFL_ERROR. CH: %d\n",i);

  //     pars = 10;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPTRIGFLFG_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPTRIGFLFG: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFLFG_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPTRIGFLFG: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPTRIGFLFG_ERROR. CH: %d\n",i);

  //     pars = 400;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENSL_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENSL: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSL_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENSL: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENSL_ERROR. CH: %d\n",i);

  //     pars = 500;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENSLSG_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENSLSG: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSLSG_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENSLSG: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENSLSG_ERROR. CH: %d\n",i);

  //     pars = 1023872;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENTAU1_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENTAU1: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENTAU1_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENTAU1: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENTAU1_ERROR. CH: %d\n",i);

  //     pars = 2048;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENGAIN1_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENGAIN1: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN1_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENGAIN1: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENGAIN1_ERROR. CH: %d\n",i);

  //     pars = 0;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENTAU2_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENTAU2: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENTAU2_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENTAU2: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENTAU2_ERROR. CH: %d\n",i);
      
  //     pars = 0;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENGAIN2_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENGAIN2: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN2_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENGAIN2: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENGAIN2_ERROR. CH: %d\n",i);

  //     pars = 485;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPENSD_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPENSD: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSD_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPENSD: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPENSD_ERROR. CH: %d\n",i);

  //     pars = 8;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPBLLEN_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPBLLEN: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPBLLEN_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPBLLEN: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPBLLEN_ERROR. CH: %d\n",i);

  //     pars = 1200;
  //     ret = NI_USB3_WriteReg(pars, SCI_REG_MCAHPBLINIB_START+i, &dt5550);
  //     if(ret!=0) printf("WRITE_REG_MCAHPBLINIB: %d\n",ret);
  //     ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPBLINIB_START+i, &dt5550);
  //     if(ret!=0) printf("READ_REG_MCAHPBLINIB: %d\n",ret);
  //     if(pars != single_reg) printf("WRITE_REG_MCAHPBLINIB_ERROR. CH: %d\n",i);
      
  //   }

  

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  /*

  // 设置输出数据的抽取因子
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_DECIMATOR, &dt5550);
  if(ret!=0) printf("DECIMATOR: %d\n",ret);
  // 设置触发之前保留的采样点数
  ret = NI_USB3_WriteReg(pre_trigger, SCI_REG_OScilloscope_0_CONFIG_PRETRIGGER, &dt5550);
  if(ret!=0) printf("PRETRIGGER: %d\n",ret);
    
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
  ret = NI_USB3_WriteReg(0x4, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_MODE, &dt5550);
  if(ret!=0) printf("TRIGGER_MODE: %d\n",ret);
  // 模拟通道触发的阈值
  ret = NI_USB3_WriteReg(600, SCI_REG_OScilloscope_0_CONFIG_TRIGGER_LEVEL, &dt5550);
  if(ret!=0) printf("TRIGGER_LEVEL: %d\n",ret);
  // 由0->1时测量
  ret = NI_USB3_WriteReg(0, SCI_REG_OScilloscope_0_CONFIG_ARM, &dt5550);
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);
  ret = NI_USB3_WriteReg(1, SCI_REG_OScilloscope_0_CONFIG_ARM, &dt5550);
  if(ret!=0) printf("CONFIG_ARM: %d\n",ret);


  // READ_STATUS [0]=1 表示可以读取数据
  int loop = 0;
  while (status_osc != 1)
    {
      ret = NI_USB3_ReadReg(&status_osc, SCI_REG_OScilloscope_0_READ_STATUS, &dt5550);
      if(ret!=0) printf("READ_STATUS: %d\n",ret);
      printf("loop  status_osc  %d %8X\n",loop, status_osc);

      if(ret!=0) {printf("loop error\n");   ;break;}
      loop++;
      usleep(1000000);
      if(loop>10) exit(0);
    }


  ret = NI_USB3_ReadReg(&position, SCI_REG_OScilloscope_0_READ_POSITION, &dt5550);
  if(ret!=0) printf("READ_POSITION: %d\n",ret);
  printf("position   %d\n",position);

  
  TCanvas *c1 = new TCanvas("c1","c1");
  c1->Divide(8,4);
  // c1->Divide(3,1);
  TGraph *gg[32];

  for (int i = 0; i < 32; ++i)
    {
      c1->cd(i+1);
      gg[i] = new TGraph();

      // SCI_REG_OScilloscope_0_FIFOADDRESS 地址起点，16bit，低位为数据，高位为ch。
      // 当前2048/32。[15：11]为通道数。0x800 * ch
      ret = NI_USB3_ReadData(data_osc,size_osc,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;

      // ret = NI_USB3_ReadData(data_osc,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;
      // ret = NI_USB3_ReadData(data_osc+size_osc/2,size_osc/2,SCI_REG_OScilloscope_0_FIFOADDRESS+0x800*i+size_osc/2,REG_ACCESS,1000, &dt5550, &read_data_osc, &valid_data_osc);
      // if(ret!=0) printf("FIFOADDRESS: %d\n",ret);
      // std::cout<<read_data_osc<<"  "<<valid_data_osc<<std::endl;


      
      ret = OSCILLOSCOPE_RECONSTRUCT(data_osc, position, pre_trigger, size_osc, read_analog, read_digital0, read_digital1, read_digital2, read_digital3);
      if(ret!=0) printf("RECONSTRUCT: %d\n",ret);
		

      for (int j = 0; j < size_osc-2; ++j)
	{
	  // gg->SetPoint(i,i,data_osc[i]);
	  gg[i]->SetPoint(j,double(j),double(read_analog[j]));
	
	  // std::cout<<i <<"  "<<read_analog[i]<<std::endl;
	}
      gg[i]->Draw("AP*");
      // if(i==0) break;
    }

  
  */
  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......
    
  /*
    
  // reset
  ret = NI_USB3_WriteReg(2,SCI_REG_List_CONFIG, &dt5550);
  if(ret!=0) printf("LIST RESET: %d\n",ret);
  // start
  ret = NI_USB3_WriteReg(2,SCI_REG_List_CONFIG, &dt5550);
  if(ret!=0) printf("LIST START1: %d\n",ret);
  ret = NI_USB3_WriteReg(1,SCI_REG_List_CONFIG, &dt5550);
  if(ret!=0) printf("LIST START2: %d\n",ret);



  FILE *fsave = NULL;
  fsave = fopen("data.dat","wb");
  if(fsave == NULL)
    {
      std::cout<<"Cannot open store file!"<<std::endl;
    }

  
  bool looplist = true;
  int countlist = 0;
  while (looplist)
    {
      if(countlist % 1000 == 0) std::cout<<"countlist: "<<countlist<<std::endl;
      
      // status
      ret = NI_USB3_ReadReg(&status_list, SCI_REG_List_STATUS,&dt5550);
      if(ret!=0) printf("LIST STATUS: %d\n",ret);

      empty_list = status_list & 0x1;
      full_list  = status_list & 0x2;
      word_list = (status_list & 0xFFFFFF00) >> 8;
      // printf("empty:%d  full:%d  word:%d\n",empty_list,full_list,word_list);

      if(empty_list==0 && word_list >= size_list)
	{
	  ret = NI_USB3_ReadData(data_list, word_list/size_list*size_list, SCI_REG_List_FIFOADDRESS, STREAMING, timeout_list, &dt5550, &read_data_list, &valid_data_list);
	  std::cout<<read_data_list<<"  "<<valid_data_list<<std::endl;
	  if(ret!=0) printf("LIST READDATA: %d\n",ret);

	  for (int i = 0; i < word_list/size_list; ++i)
	    {
	      if(data_list[8*i+5]!=0xFFFFFFFF || data_list[8*i+6]!=0xFFFFFFFF || data_list[8*i+7]!=0xFFFFFFFF)
		std::cout<<data_list[8*i+5]<<"  "<<data_list[8*i+6]<<"  "<<data_list[8*i+7]<<std::endl;
	    }
	  
	  
	  size_t n = fwrite(data_list,4,word_list/size_list*size_list,fsave);
	  if(n != word_list/size_list*size_list)
	    {
	      std::cout<<"Not All Data has been stored!"<<std::endl;
	    }

	  
	  countlist += word_list/size_list;
	  if(countlist >= 100) break;
	}
    }

  fclose(fsave);

  */

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  // TCanvas *c2 = new TCanvas("c2","c2");
  // TH1I *hspe = new TH1I("hspe","",16384,0,16384);


  
  // uint32_t status_spectrum= 0;
  // int32_t Rebin = 0;
  // int32_t LimitMode = 0;
  // int32_t LimitValue = 0;
  // uint32_t data_spectrum[16384];
  // uint32_t read_data_spectrum;
  // uint32_t valid_data_spectrum;
  // uint32_t bins = 16384;
  // int32_t timeout_spectrum = 1000;
  // uint32_t spectrum[16384];

  // // set pars
  // ret = NI_USB3_WriteReg(Rebin, SCI_REG_Spectrum_0_CONFIG_REBIN, &dt5550);
  // if(ret!=0) printf("SCI_REG_Spectrum_0_CONFIG_REBIN: %d\n",ret);
  // int32_t limit = 0;
  // limit = (1 << (LimitMode + 29)) + LimitValue; 
  // ret = NI_USB3_WriteReg(limit, SCI_REG_Spectrum_0_CONFIG_LIMIT, &dt5550);
  // if(ret!=0) printf("SCI_REG_Spectrum_0_CONFIG_LIMIT: %d\n",ret);

  // // flush
  // ret = NI_USB3_WriteReg(1,SCI_REG_Spectrum_0_CONFIG, &dt5550);
  // if(ret!=0) printf("SCI_REG_Spectrum_FLUSH: %d\n",ret);
  
  // // reset
  // ret = NI_USB3_WriteReg(2,SCI_REG_Spectrum_0_CONFIG, &dt5550);
  // if(ret!=0) printf("SCI_REG_Spectrum_RESET: %d\n",ret);
  
  // // start
  // ret = NI_USB3_WriteReg(4,SCI_REG_Spectrum_0_CONFIG, &dt5550);
  // if(ret!=0) printf("SCI_REG_Spectrum_START: %d\n",ret);
  // if(ret == 0)
  //   {
  //     usleep(20000000);
  //     ret = NI_USB3_ReadReg(&status_spectrum, SCI_REG_Spectrum_0_STATUS, &dt5550);
  //     if(ret!=0) printf("SCI_REG_Spectrum_STATUS: %d\n",ret);
  //     while (status_spectrum != 0)
  // 	{
  // 	  usleep(20000000);
  // 	  ret = NI_USB3_ReadData(data_spectrum, bins, SCI_REG_Spectrum_0_FIFOADDRESS, REG_ACCESS, timeout_spectrum, &dt5550, &read_data_spectrum, &valid_data_spectrum);
  // 	  if(ret!=0) printf("READ_Spectrum_ERROR: %d\n",ret);
  // 	  for (int i = 0; i < bins; i++)
  // 	    {
  // 	      spectrum[i] = data_spectrum[i];
  // 	      hspe->SetBinContent(i+1,double(spectrum[i]));
  // 	    }
  // 	  c2->cd();
  // 	  hspe->Draw();
  // 	  c2->Update();
  // 	}


  //     ret = NI_USB3_WriteReg(0,SCI_REG_Spectrum_0_CONFIG, &dt5550);
  //     if(ret!=0) printf("SCI_REG_Spectrum_STOP: %d\n",ret);
  //   }


  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  NI_USB3_CloseConnection(&dt5550);
  printf("\n\n");





  theApp->Run();
  delete theApp;
    
  return 0;
}
