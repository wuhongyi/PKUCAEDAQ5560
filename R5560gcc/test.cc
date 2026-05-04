// test.cc --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 12月 11 15:25:39 2021 (+0800)
// Last-Updated: 六 12月 11 16:50:47 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 4
// URL: http://wuhongyi.cn 

#include "RegisterFile.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>



#define BOARD_IP_ADDRESS "162.105.54.156"
//1 162.105.54.156
//2
//3
//4 162.105.54.42


typedef struct {
	int Csocket;
	int connected;
	uint32_t __IICBASEADDRESS;
	uint32_t __IICBASEADDRESS_STATUS;
	SOCKET_TYPE socketType;
	tZMQEndpoint *zmq;
} tR5560_Handle;

#define NI_HANDLE tR5560_Handle


SCILIB int OSCILLOSCOPE_Oscilloscope_RECONSTRUCT(uint32_t *data_osc, uint32_t position, int32_t pre_trigger, uint32_t *read_analog, uint32_t *read_digital0, uint32_t *read_digital1, uint32_t *read_digital2, uint32_t *read_digital3)
{
int n_ch = 1;
int n_samples = 2048;
if (position > n_samples) return -1;
for(int n=0; n< n_ch; n++)
{
	int current = position - pre_trigger;
	if (current > 0)
    {
	    int k = 0;
	    for (int i = current; i < n_samples - 1; i++)
        {
		 read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
		 read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
		 read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
		 read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
		 read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
             k++;
        }
	    for (int i = 0; i < current - 1; i++)
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
	    for (int i = n_samples+current; i < n_samples - 1; i++)
        {
		 read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
		 read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
		 read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
		 read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
		 read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
             k++;
        }
	    for (int i = 0; i < n_samples + current - 1; i++)
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
  NI_HANDLE handle;
  int ret;
  uint32_t    val;

  if(R5560_ConnectTCP(BOARD_IP_ADDRESS, 8888, &handle) != 0) { printf("Unable to connect to the board!\n"); return (-1); };

  uint32_t status_osc = 0;
  uint32_t data_osc[1024];
  uint32_t read_data_osc;
  uint32_t valid_data_osc;
  uint32_t position = 0;
  int32_t decimator = 0;
  int32_t pre_trigger = 100;
  int32_t software_trigger = 0;
  int32_t analog_trigger = 1;
  int32_t digital0_trigger = 0;
  int32_t digital1_trigger = 0;
  int32_t digital2_trigger = 0;
  int32_t digital3_trigger = 0;
  int32_t trigger_channel = 0;
  int32_t trigger_edge = 0;
  int32_t trigger_level = 1000;
  int32_t timeout_osc = 1000;
  int32_t size_osc = 1024;
  uint32_t read_analog[1024];
  uint32_t read_digital0[1024];
  uint32_t read_digital1[1024];
  uint32_t read_digital2[1024];
  uint32_t read_digital3[1024];
  int Osc_Events = 10;
  int e = 0;

  int ch = 0;
  
  while (e < Osc_Events)
    {
      int32_t triggermode = 0;
      int r_decimator = NI_WriteReg(decimator, SCI_REG_Oscilloscope_0_CONFIG_DECIMATOR+0x10000*ch, handle);
      int r_pre = NI_WriteReg(pre, SCI_REG_Oscilloscope_0_CONFIG_PRETRIGGER+0x10000*ch, handle);
      int r_triglevel = NI_WriteReg(trigger_level, SCI_REG_Oscilloscope_0_CONFIG_TRIGGER_LEVEL+0x10000*ch, handle);
      triggermode = (trigger_channel << 8)  + (software_trigger << 7 ) + (trigger_edge << 3) + (software_trigger << 1) + analog_trigger + (digital0_trigger << 2) + (digital1_trigger << 2) + digital1_trigger + (digital2_trigger << 2) + (digital2_trigger << 1) + (digital3_trigger << 2) + (digital3_trigger << 1) + digital3_trigger ; 
      int r_triggermode = NI_WriteReg(triggermode, SCI_REG_Oscilloscope_0_CONFIG_TRIGGER_MODE+0x10000*ch, handle);
      if (r_decimator == 0 & r_pre == 0 & r_triglevel == 0 & r_triggermode == 0)
	printf("Set Parameters Success\n");
      else
	printf("Set Parameters Error\n");


      int r1 = NI_WriteReg(0,SCI_REG_Oscilloscope_0_CONFIG_ARM+0x10000*ch, handle);
      int r2 = NI_WriteReg(1,SCI_REG_Oscilloscope_0_CONFIG_ARM+0x10000*ch, handle);
      if ((r1 == 0) && (r2 == 0))
	printf("Start Success\n");
      else
	printf("Start Error\n");



      while (status_osc != 1)
	if(NI_ReadReg(&status_osc, SCI_REG_Oscilloscope_0_READ_STATUS+0x10000*ch, handle) != 0) printf("Status Error");


      if(NI_ReadReg(&position, SCI_REG_Oscilloscope_0_READ_POSITION+0x10000*ch, handle) != 0) printf("Position Error");


      if(NI_ReadData(data_osc,  size_osc, SCI_REG_Oscilloscope_0_FIFOADDRESS+0x10000*ch,  handle, read_data) != 0) printf("Get Data Error");
      

      
      if (OSCILLOSCOPE_Oscilloscope_RECONSTRUCT(data_osc, position, pre_trigger, read_analog, read_digital0, read_digital1, read_digital2, read_digital3) != 0) printf("Reconstruction Error");


      
      e++;
    }
  printf("Download Finished");
  
  
  return 0;
}

// 
// test.cc ends here
