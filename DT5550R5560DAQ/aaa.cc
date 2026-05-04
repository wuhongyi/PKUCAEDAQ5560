// aaa.cc --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 二 3月 16 14:29:56 2021 (+0800)
// Last-Updated: 二 3月 16 14:32:37 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 2
// URL: http://wuhongyi.cn 

#include <iostream>
#include <fstream>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
  std::ofstream datawrite;
  datawrite.open("data.dat");//ios::bin ios::app
  if(!datawrite.is_open())
    {
      std::cout<<"can't open file."<<std::endl;
    }

  int aa[8];
  aa[0] = 0;
  for (int i = 0; i < 100; ++i)
    {
      datawrite<<aa[0]<<" "<<aa[1]<<" "<<aa[2]<<" "<<aa[3]<<" "<<aa[4]<<std::endl;
    }

  datawrite.close();


  
  return 0;
}




// 
// aaa.cc ends here
