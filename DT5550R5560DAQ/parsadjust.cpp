// parsadjust.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 4月  3 15:07:58 2021 (+0800)
// Last-Updated: 五 12月 24 21:06:42 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 19
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

#define QDC
#define MCAHP

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

  int nch = -1;
  std::vector<uint32_t>* vthre = new std::vector<uint32_t>;
  std::vector<uint32_t>* vbllen = new std::vector<uint32_t>;
  std::vector<uint32_t>* vblhold = new std::vector<uint32_t>;
  std::vector<uint32_t>* vqdclen = new std::vector<uint32_t>;
  std::vector<uint32_t>* vqdcpre = new std::vector<uint32_t>;
  std::vector<uint32_t>* vqdcgain = new std::vector<uint32_t>;
  std::vector<uint32_t>* vqdcpuinib = new std::vector<uint32_t>;

  std::vector<uint32_t>* venpre = new std::vector<uint32_t>;
  std::vector<uint32_t>* vensl = new std::vector<uint32_t>;
  std::vector<uint32_t>* venslsg = new std::vector<uint32_t>;


  std::vector<uint32_t>* vmcahptrigfl = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahptrigflfg = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpensl = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpenslsg = new std::vector<uint32_t>;
  
  std::vector<uint32_t>* vmcahpentau1 = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpengain1 = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpentau2 = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpengain2 = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpensd = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpbllen = new std::vector<uint32_t>;
  std::vector<uint32_t>* vmcahpblinib = new std::vector<uint32_t>;
  
  
  nch = wuReadData::ReadVector("Threshold","pars.txt",vthre); 
  if(nch != 32) printf("pars Threshold error ...\n");
  nch = wuReadData::ReadVector("BLLEN","pars.txt",vbllen); 
  if(nch != 32) printf("pars BLLEN error ...\n");
  nch = wuReadData::ReadVector("BLHOLD","pars.txt",vblhold);
  if(nch != 32) printf("pars BLHOLD error ...\n");
  nch = wuReadData::ReadVector("QDCLEN","pars.txt",vqdclen);
  if(nch != 32) printf("pars QDCLEN error ...\n");
  nch = wuReadData::ReadVector("QDCPRE","pars.txt",vqdcpre);
  if(nch != 32) printf("pars QDCPRE error ...\n");
  nch = wuReadData::ReadVector("QDCGAIN","pars.txt",vqdcgain);
  if(nch != 32) printf("pars QDCGAIN error ...\n");
  nch = wuReadData::ReadVector("QDCPUINIB","pars.txt",vqdcpuinib); 
  if(nch != 32) printf("pars QDCPUINIB error ...\n");


  nch = wuReadData::ReadVector("ENPRE","pars.txt",venpre); 
  if(nch != 32) printf("pars ENPRE error ...\n"); 
  nch = wuReadData::ReadVector("ENSL","pars.txt",vensl); 
  if(nch != 32) printf("pars ENSL error ...\n"); 
  nch = wuReadData::ReadVector("ENSLSG","pars.txt",venslsg); 
  if(nch != 32) printf("pars ENSLSG error ...\n");   


  nch = wuReadData::ReadVector("MCAHPTRIGFL","pars.txt",vmcahptrigfl); 
  if(nch != 32) printf("pars MCAHPTRIGFL error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPTRIGFLFG","pars.txt",vmcahptrigflfg); 
  if(nch != 32) printf("pars MCAHPTRIGFLFG error ...\n");   
  nch = wuReadData::ReadVector("MCAHPENSL","pars.txt",vmcahpensl); 
  if(nch != 32) printf("pars MCAHPENSL error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPENSLSG","pars.txt",vmcahpenslsg); 
  if(nch != 32) printf("pars MCAHPENSLSG error ...\n");   
  nch = wuReadData::ReadVector("MCAHPENTAU1","pars.txt",vmcahpentau1); 
  if(nch != 32) printf("pars MCAHPENTAU1 error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPENGAIN1","pars.txt",vmcahpengain1); 
  if(nch != 32) printf("pars MCAHPENGAIN1 error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPENTAU2","pars.txt",vmcahpentau2); 
  if(nch != 32) printf("pars MCAHPENTAU2 error ...\n");   
  nch = wuReadData::ReadVector("MCAHPENGAIN2","pars.txt",vmcahpengain2); 
  if(nch != 32) printf("pars MCAHPENGAIN2 error ...\n");
  nch = wuReadData::ReadVector("MCAHPENSD","pars.txt",vmcahpensd); 
  if(nch != 32) printf("pars MCAHPENSD error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPBLLEN","pars.txt",vmcahpbllen); 
  if(nch != 32) printf("pars MCAHPBLLEN error ...\n"); 
  nch = wuReadData::ReadVector("MCAHPBLINIB","pars.txt",vmcahpblinib); 
  if(nch != 32) printf("pars MCAHPBLINIB error ...\n"); 

  
  
  for (int i = 0; i < 32; ++i)
    {
#ifdef DT5550
      
      ret = NI_USB3_WriteReg(vthre->at(i),SCI_REG_THRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_THRE: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_THRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_THRE: %d\n",ret);
      if(vthre->at(i) != single_reg) printf("WRITE_REG_THRE_ERROR. CH: %d\n",i);


      ret = NI_USB3_WriteReg(vbllen->at(i),SCI_REG_BLLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_BLLEN: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_BLLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_BLLEN: %d\n",ret);
      if(vbllen->at(i) != single_reg) printf("WRITE_REG_BLLEN_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vblhold->at(i),SCI_REG_BLHOLD_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_BLHOLD: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_BLHOLD_START+i, &handle);
      if(ret != 0) printf("READ_REG_BLHOLD: %d\n",ret);
      if(vblhold->at(i) != single_reg) printf("WRITE_REG_BLHOLD_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vqdclen->at(i),SCI_REG_QDCLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCLEN: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_QDCLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCLEN: %d\n",ret);
      if(vqdclen->at(i) != single_reg) printf("WRITE_REG_QDCLEN_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vqdcpre->at(i),SCI_REG_QDCPRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCPRE: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_QDCPRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCPRE: %d\n",ret);
      if(vqdcpre->at(i) != single_reg) printf("WRITE_REG_QDCPRE_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vqdcgain->at(i),SCI_REG_QDCGAIN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCGAIN: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_QDCGAIN_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCGAIN: %d\n",ret);
      if(vqdcgain->at(i) != single_reg) printf("WRITE_REG_QDCGAIN_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vqdcpuinib->at(i),SCI_REG_QDCPUINIB_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCPUINIB: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_QDCPUINIB_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCPUINIB: %d\n",ret);
      if(vqdcpuinib->at(i) != single_reg) printf("WRITE_REG_QDCPUINIB_ERROR. CH: %d\n",i);



      ret = NI_USB3_WriteReg(venpre->at(i),SCI_REG_ENPRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENPRE: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_ENPRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENPRE: %d\n",ret);
      if(venpre->at(i) != single_reg) printf("WRITE_REG_ENPRE_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vensl->at(i),SCI_REG_ENSL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENSL: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_ENSL_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENSL: %d\n",ret);
      if(vensl->at(i) != single_reg) printf("WRITE_REG_ENSL_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(venslsg->at(i),SCI_REG_ENSLSG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENSLSG: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_ENSLSG_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENSLSG: %d\n",ret);
      if(venslsg->at(i) != single_reg) printf("WRITE_REG_ENSLSG_ERROR. CH: %d\n",i);  

      

      ret = NI_USB3_WriteReg(vmcahptrigfl->at(i),SCI_REG_MCAHPTRIGFL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPTRIGFL: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFL_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPTRIGFL: %d\n",ret);
      if(vmcahptrigfl->at(i) != single_reg) printf("WRITE_REG_MCAHPTRIGFL_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahptrigflfg->at(i),SCI_REG_MCAHPTRIGFLFG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPTRIGFLFG: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFLFG_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPTRIGFLFG: %d\n",ret);
      if(vmcahptrigflfg->at(i) != single_reg) printf("WRITE_REG_MCAHPTRIGFLFG_ERROR. CH: %d\n",i);
      
      ret = NI_USB3_WriteReg(vmcahpensl->at(i),SCI_REG_MCAHPENSL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSL: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSL_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSL: %d\n",ret);
      if(vmcahpensl->at(i) != single_reg) printf("WRITE_REG_MCAHPENSL_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpenslsg->at(i),SCI_REG_MCAHPENSLSG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSLSG: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSLSG_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSLSG: %d\n",ret);
      if(vmcahpenslsg->at(i) != single_reg) printf("WRITE_REG_MCAHPENSLSG_ERROR. CH: %d\n",i);



      ret = NI_USB3_WriteReg(vmcahpentau1->at(i),SCI_REG_MCAHPENTAU1_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENTAU1: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENTAU1_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENTAU1: %d\n",ret);
      if(vmcahpentau1->at(i) != single_reg) printf("WRITE_REG_MCAHPENTAU1_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpengain1->at(i),SCI_REG_MCAHPENGAIN1_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENGAIN1: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN1_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENGAIN1: %d\n",ret);
      if(vmcahpengain1->at(i) != single_reg) printf("WRITE_REG_MCAHPENGAIN1_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpentau2->at(i),SCI_REG_MCAHPENTAU2_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENTAU2: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENTAU2_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENTAU2: %d\n",ret);
      if(vmcahpentau2->at(i) != single_reg) printf("WRITE_REG_MCAHPENTAU2_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpengain2->at(i),SCI_REG_MCAHPENGAIN2_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENGAIN2: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN2_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENGAIN2: %d\n",ret);
      if(vmcahpengain2->at(i) != single_reg) printf("WRITE_REG_MCAHPENGAIN2_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpensd->at(i),SCI_REG_MCAHPENSD_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSD: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPENSD_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSD: %d\n",ret);
      if(vmcahpensd->at(i) != single_reg) printf("WRITE_REG_MCAHPENSD_ERROR. CH: %d\n",i);
      
      ret = NI_USB3_WriteReg(vmcahpbllen->at(i),SCI_REG_MCAHPBLLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPBLLEN: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPBLLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPBLLEN: %d\n",ret);
      if(vmcahpbllen->at(i) != single_reg) printf("WRITE_REG_MCAHPBLLEN_ERROR. CH: %d\n",i);

      ret = NI_USB3_WriteReg(vmcahpblinib->at(i),SCI_REG_MCAHPBLINIB_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPBLINIB: %d\n",ret);
      ret = NI_USB3_ReadReg(&single_reg, SCI_REG_MCAHPBLINIB_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPBLINIB: %d\n",ret);
      if(vmcahpblinib->at(i) != single_reg) printf("WRITE_REG_MCAHPBLINIB_ERROR. CH: %d\n",i); 
      
#endif

#ifdef R5560

      ret = NI_WriteReg(vthre->at(i),SCI_REG_THRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_THRE: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_THRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_THRE: %d\n",ret);
      if(vthre->at(i) != single_reg) printf("WRITE_REG_THRE_ERROR. CH: %d\n",i);


      ret = NI_WriteReg(vbllen->at(i),SCI_REG_BLLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_BLLEN: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_BLLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_BLLEN: %d\n",ret);
      if(vbllen->at(i) != single_reg) printf("WRITE_REG_BLLEN_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vblhold->at(i),SCI_REG_BLHOLD_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_BLHOLD: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_BLHOLD_START+i, &handle);
      if(ret != 0) printf("READ_REG_BLHOLD: %d\n",ret);
      if(vblhold->at(i) != single_reg) printf("WRITE_REG_BLHOLD_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vqdclen->at(i),SCI_REG_QDCLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCLEN: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_QDCLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCLEN: %d\n",ret);
      if(vqdclen->at(i) != single_reg) printf("WRITE_REG_QDCLEN_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vqdcpre->at(i),SCI_REG_QDCPRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCPRE: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_QDCPRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCPRE: %d\n",ret);
      if(vqdcpre->at(i) != single_reg) printf("WRITE_REG_QDCPRE_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vqdcgain->at(i),SCI_REG_QDCGAIN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCGAIN: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_QDCGAIN_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCGAIN: %d\n",ret);
      if(vqdcgain->at(i) != single_reg) printf("WRITE_REG_QDCGAIN_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vqdcpuinib->at(i),SCI_REG_QDCPUINIB_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_QDCPUINIB: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_QDCPUINIB_START+i, &handle);
      if(ret != 0) printf("READ_REG_QDCPUINIB: %d\n",ret);
      if(vqdcpuinib->at(i) != single_reg) printf("WRITE_REG_QDCPUINIB_ERROR. CH: %d\n",i);



      ret = NI_WriteReg(venpre->at(i),SCI_REG_ENPRE_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENPRE: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_ENPRE_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENPRE: %d\n",ret);
      if(venpre->at(i) != single_reg) printf("WRITE_REG_ENPRE_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vensl->at(i),SCI_REG_ENSL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENSL: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_ENSL_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENSL: %d\n",ret);
      if(vensl->at(i) != single_reg) printf("WRITE_REG_ENSL_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(venslsg->at(i),SCI_REG_ENSLSG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_ENSLSG: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_ENSLSG_START+i, &handle);
      if(ret != 0) printf("READ_REG_ENSLSG: %d\n",ret);
      if(venslsg->at(i) != single_reg) printf("WRITE_REG_ENSLSG_ERROR. CH: %d\n",i);  

      

      ret = NI_WriteReg(vmcahptrigfl->at(i),SCI_REG_MCAHPTRIGFL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPTRIGFL: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFL_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPTRIGFL: %d\n",ret);
      if(vmcahptrigfl->at(i) != single_reg) printf("WRITE_REG_MCAHPTRIGFL_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahptrigflfg->at(i),SCI_REG_MCAHPTRIGFLFG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPTRIGFLFG: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPTRIGFLFG_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPTRIGFLFG: %d\n",ret);
      if(vmcahptrigflfg->at(i) != single_reg) printf("WRITE_REG_MCAHPTRIGFLFG_ERROR. CH: %d\n",i);
      
      ret = NI_WriteReg(vmcahpensl->at(i),SCI_REG_MCAHPENSL_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSL: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENSL_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSL: %d\n",ret);
      if(vmcahpensl->at(i) != single_reg) printf("WRITE_REG_MCAHPENSL_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpenslsg->at(i),SCI_REG_MCAHPENSLSG_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSLSG: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENSLSG_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSLSG: %d\n",ret);
      if(vmcahpenslsg->at(i) != single_reg) printf("WRITE_REG_MCAHPENSLSG_ERROR. CH: %d\n",i);  

      ret = NI_WriteReg(vmcahpentau1->at(i),SCI_REG_MCAHPENTAU1_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENTAU1: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENTAU1_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENTAU1: %d\n",ret);
      if(vmcahpentau1->at(i) != single_reg) printf("WRITE_REG_MCAHPENTAU1_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpengain1->at(i),SCI_REG_MCAHPENGAIN1_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENGAIN1: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN1_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENGAIN1: %d\n",ret);
      if(vmcahpengain1->at(i) != single_reg) printf("WRITE_REG_MCAHPENGAIN1_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpentau2->at(i),SCI_REG_MCAHPENTAU2_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENTAU2: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENTAU2_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENTAU2: %d\n",ret);
      if(vmcahpentau2->at(i) != single_reg) printf("WRITE_REG_MCAHPENTAU2_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpengain2->at(i),SCI_REG_MCAHPENGAIN2_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENGAIN2: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENGAIN2_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENGAIN2: %d\n",ret);
      if(vmcahpengain2->at(i) != single_reg) printf("WRITE_REG_MCAHPENGAIN2_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpensd->at(i),SCI_REG_MCAHPENSD_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPENSD: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPENSD_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPENSD: %d\n",ret);
      if(vmcahpensd->at(i) != single_reg) printf("WRITE_REG_MCAHPENSD_ERROR. CH: %d\n",i);
      
      ret = NI_WriteReg(vmcahpbllen->at(i),SCI_REG_MCAHPBLLEN_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPBLLEN: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPBLLEN_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPBLLEN: %d\n",ret);
      if(vmcahpbllen->at(i) != single_reg) printf("WRITE_REG_MCAHPBLLEN_ERROR. CH: %d\n",i);

      ret = NI_WriteReg(vmcahpblinib->at(i),SCI_REG_MCAHPBLINIB_START+i, &handle);
      if(ret != 0) printf("WRITE_REG_MCAHPBLINIB: %d\n",ret);
      ret = NI_ReadReg(&single_reg, SCI_REG_MCAHPBLINIB_START+i, &handle);
      if(ret != 0) printf("READ_REG_MCAHPBLINIB: %d\n",ret);
      if(vmcahpblinib->at(i) != single_reg) printf("WRITE_REG_MCAHPBLINIB_ERROR. CH: %d\n",i); 
          
#endif        
    }
  
  
  if(vthre) delete vthre;
  if(vbllen) delete vbllen;
  if(vblhold) delete vblhold;
  if(vqdclen) delete vqdclen;
  if(vqdcpre) delete vqdcpre;
  if(vqdcgain) delete vqdcgain;
  if(vqdcpuinib) delete vqdcpuinib;

  if(vmcahptrigfl) delete vmcahptrigfl;
  if(vmcahptrigflfg) delete vmcahptrigflfg;
  if(vmcahpensl) delete vmcahpensl;
  if(vmcahpenslsg) delete vmcahpenslsg;

  if(vmcahpentau1) delete vmcahpentau1;
  if(vmcahpengain1) delete vmcahpengain1;
  if(vmcahpentau2) delete vmcahpentau2;
  if(vmcahpengain2) delete vmcahpengain2;
  if(vmcahpensd) delete vmcahpensd;
  if(vmcahpbllen) delete vmcahpbllen;
  if(vmcahpblinib) delete vmcahpblinib;

  
  if(venpre) delete venpre;  
  if(vensl) delete vensl; 
  if(venslsg) delete venslsg;
  
#ifdef DT5550
  ret = NI_USB3_WriteReg(0xFFFFFFFF,REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("WRITE_REG_RESET: %d\n",ret);
  ret = NI_USB3_ReadReg(&single_reg, REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("READ_REG_RESET: %d\n",ret);
  if(0xFFFFFFFF != single_reg) printf("WRITE_REG_RESET_ERROR.\n");
#endif
#ifdef R5560
  ret = NI_WriteReg(0xFFFFFFFF,REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("WRITE_REG_RESET: %d\n",ret);
  ret = NI_ReadReg(&single_reg, REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("READ_REG_RESET: %d\n",ret);
  if(0xFFFFFFFF != single_reg) printf("WRITE_REG_RESET_ERROR.\n");
#endif  
  usleep(100);
#ifdef DT5550
  ret = NI_USB3_WriteReg(0x0,REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("WRITE_REG_RESET: %d\n",ret);
  ret = NI_USB3_ReadReg(&single_reg, REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("READ_REG_RESET: %d\n",ret);
  if(0x0 != single_reg) printf("WRITE_REG_RESET_ERROR.\n");
#endif
#ifdef R5560
  ret = NI_WriteReg(0x0,REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("WRITE_REG_RESET: %d\n",ret);
  ret = NI_ReadReg(&single_reg, REG_RESET_SINGLE, &handle);
  if(ret != 0) printf("READ_REG_RESET: %d\n",ret);
  if(0x0 != single_reg) printf("WRITE_REG_RESET_ERROR.\n");
#endif   
  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

#ifdef DT5550
  NI_USB3_CloseConnection(&dt5550);
#endif
  printf("\n");
  
  
  return 0;
}
  
// 
// parsadjust.cpp ends here
