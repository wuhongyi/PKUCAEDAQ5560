// list.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 日 4月 18 18:05:19 2021 (+0800)
// Last-Updated: 六 10月  9 15:53:41 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 14
// URL: http://wuhongyi.cn 

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
#include <fstream>
#include <istream>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
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

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  uint32_t status_list = 0;
  uint32_t empty_list = 0;
  uint32_t full_list = 0;
  uint32_t word_list = 0;
  uint32_t data_list[65536];
  uint32_t read_data_list;
  uint32_t valid_data_list;
  uint32_t size_list = 8;
  int32_t timeout_list = 1000;

  
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

  int event = 1000000;
  
  bool looplist = true;
  int countlist = 0;
  while (looplist)
    {
      if(countlist % 10000 == 0) std::cout<<"countlist: "<<countlist<<std::endl;

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
	  // std::cout<<read_data_list<<"  "<<valid_data_list<<std::endl;
	  if(ret!=0) printf("LIST READDATA: %d\n",ret);

	  for (int i = 0; i < word_list/size_list; ++i)
	    {
	      if(data_list[8*i+6]!=0xFFFFFFFF || data_list[8*i+7]!=0xFFFFFFFF)
	  	std::cout<<data_list[8*i+6]<<"  "<<data_list[8*i+7]<<std::endl;
	    }
	  
	  size_t n = fwrite(data_list,4,word_list/size_list*size_list,fsave);
	  if(n != word_list/size_list*size_list)
	    {
	      std::cout<<"Not All Data has been stored!"<<std::endl;
	    }
	  
	  countlist += word_list/size_list;
	  if(countlist >= event) break;
	}
    }

  fclose(fsave);


  
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  NI_USB3_CloseConnection(&dt5550);
  printf("\n");
  
  
  return 0;
}  


// 
// list.cpp ends here
