// RegisterFile.h --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 六 12月 11 15:37:59 2021 (+0800)
// Last-Updated: 六 12月 11 16:50:23 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 1
// URL: http://wuhongyi.cn 

#ifndef _REGISTERFILE_H_
#define _REGISTERFILE_H_

#define SCI_REG_OScilloscope_0_FIFOADDRESS 0x200000

#define SCI_REG_Oscilloscope_0_READ_STATUS 0x0000 //+0x10000*ch
#define SCI_REG_Oscilloscope_0_READ_POSITION 0x0001
#define SCI_REG_Oscilloscope_0_CONFIG_TRIGGER_MODE 0x0002
#define SCI_REG_Oscilloscope_0_CONFIG_PRETRIGGER 0x0003
#define SCI_REG_Oscilloscope_0_CONFIG_TRIGGER_LEVEL 0x0004
#define SCI_REG_Oscilloscope_0_CONFIG_ARM 0x0005
#define SCI_REG_Oscilloscope_0_CONFIG_DECIMATOR 0x0006




#endif /* _REGISTERFILE_H_ */

// 
// RegisterFile.h ends here
