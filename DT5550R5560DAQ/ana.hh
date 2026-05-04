// ana.hh --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 三 10月 13 19:48:19 2021 (+0800)
// Last-Updated: 三 10月 13 19:52:50 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 4
// URL: http://wuhongyi.cn 

#ifndef _ANA_H_
#define _ANA_H_

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


int OSCILLOSCOPE_RECONSTRUCT(uint32_t *data_osc, uint32_t position, uint32_t pre_trigger, uint32_t n_samples, uint32_t *read_analog, uint32_t *read_digital0, uint32_t *read_digital1, uint32_t *read_digital2, uint32_t *read_digital3)
{
  int n_ch = 1;
  // int n_samples = 2048;
  if (position > n_samples) return -1;
  for(int n=0; n< n_ch; n++)
    {
      // int current = position - pre_trigger-1;
      int current = position - pre_trigger;
      if (current > 0)
	{
	  int k = 0;
	  // for (int i = current; i < n_samples; i++)
	  for (int i = current; i < n_samples-1; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	  // for (int i = 0; i < current; i++)
	  for (int i = 0; i < current-1; i++)
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
	  // for (int i = n_samples+current; i < n_samples; i++)
	  for (int i = n_samples+current; i < n_samples-1; i++)
	    {
	      read_analog[k + (n_samples*n)] = data_osc[i + (n_samples*n)] & 65535;
	      read_digital0[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 16 & 1;
	      read_digital1[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 17 & 1;
	      read_digital2[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 18 & 1;
	      read_digital3[k + (n_samples*n)] = data_osc[i + (n_samples*n)] >> 19 & 1;
	      k++;
	    }
	  // for (int i = 0; i < n_samples + current; i++)
	  for (int i = n_samples+current; i < n_samples-1; i++)
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

#endif /* _ANA_H_ */

// 
// ana.hh ends here
