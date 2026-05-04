// main.cc --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 二 3月 16 17:28:34 2021 (+0800)
// Last-Updated: 一 7月 26 21:01:13 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 120
// URL: http://wuhongyi.cn 

// g++ `root-config --cflags --glibs` -l Spectrum main.cc -o test
// g++ `root-config --cflags` main.cc -o test `root-config --glibs`
#include "TRint.h"
#include "TCanvas.h"
#include "TH1.h"
#include "TH2.h"
#include "TMath.h"

#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> 
#include <string.h>
#include <stdio.h>
#include <iostream>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

int main(int argc, char *argv[])
{
  // Create an interactive ROOT application
  TRint *theApp = new TRint("Rint", &argc, argv);

  TCanvas *canvas = new TCanvas("canvas","c1");

  TH1I *hh[32];
  TH1I *hh1[32]; 
  TH2I *hh2[32];
  TH2I *hh3[32];
  
  hh[0] = new TH1I("hh_00","",10000,0,10000);
  hh[1] = new TH1I("hh_01","",10000,0,10000);
  hh1[0] = new TH1I("hh1_00","",10000,0,10000);
  hh1[1] = new TH1I("hh1_01","",10000,0,10000);
  hh2[0] = new TH2I("hh2_00","",2000,0,10000,2000,0,2000);
  hh2[1] = new TH2I("hh2_01","",2000,0,10000,2000,0,2000);
  hh3[0] = new TH2I("hh3_00","",2000,0,10000,2000,0,2000);
  hh3[1] = new TH2I("hh3_01","",2000,0,10000,2000,0,2000);

  TH1I *df = new TH1I("df","",10000,0,1000000);
  TH2I *df2d = new TH2I("","",4000,0,500000,2000,0,2000);
  
  int fd = -1;
  if((fd = open("data.dat",O_RDONLY))<0)
    {
      printf("cannot open file \n");
    }

  unsigned int buff[8];

  Long64_t ts_curr,ts_last;
  Long64_t deltat;
  int ch;
  int pu;
  int q1,q2,qg;
  int bl[32] = {
		-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1  };
  
  double tau[32] = {//us
		    49.5, 63, 50, 50, 50, 50, 50, 50,
		    50, 50, 50, 50, 50, 50, 50, 50,
		    50, 50, 50, 50, 50, 50, 50, 50,
		    50, 50, 50, 50, 50, 50, 50, 50  };
  
  int parM[32] = {
		  400, 400, 400, 400, 400, 400, 400, 400,
		  400, 400, 400, 400, 400, 400, 400, 400,
		  400, 400, 400, 400, 400, 400, 400, 400,
		  400, 400, 400, 400, 400, 400, 400, 400 };
  
  int parK[32] = {
		  100, 100, 100, 100, 100, 100, 100, 100,
		  100, 100, 100, 100, 100, 100, 100, 100,
		  100, 100, 100, 100, 100, 100, 100, 100,
		  100, 100, 100, 100, 100, 100, 100, 100 };
  
  double pare[32];
  double b1[32];
  double c0[32], c1[32], c2[32];
  for (int i = 0; i < 32; ++i)
    {
      pare[i] = TMath::Exp(-(parM[i]+parK[i])*12.5/1000/tau[i]);//e^{-(M+K/tau)}
      b1[i] = TMath::Exp(-12.5/1000/tau[i]);//e^{-\Delta t/tau}

      c0[i] = -TMath::Power(b1[i], (parM[i]))/(1-TMath::Power(b1[i], parM[i]));
      c1[i] = 1;
      c2[i] = 1.0/(1-TMath::Power(b1[i], parM[i]));
    }

  double ene1;
  double ene2;
  
  while(read(fd,&buff,4*8))
    {
      
      ch = (buff[0] & 0xF800)>>11;
      pu = (buff[0] & 0x4)>>2;
      ts_curr = ((Long64_t(buff[0])&0xFFFF0000)>>16)<<32;
      ts_curr += buff[1];
      ts_last = buff[2];
      if(buff[1] >= buff[2])
	deltat = buff[1]- buff[2];
      else
	deltat = Long64_t(0x100000000) + buff[1]- buff[2];
      q1 = buff[3] & 0x3FFFFFF;
      q2 = buff[4] & 0x3FFFFFF;
      qg = buff[5] & 0x3FFFFFF;

      if(buff[6]!=0xFFFFFFFF ||buff[7]!=0xFFFFFFFF)
	std::cout<<buff[6]<<"  "<<buff[7]<<std::endl;

      // if(ch==0) std::cout<<"ch0  "<<pu<<"  "<<bl[0]<<"  "<<deltat<<"  "<<q1<<"  "<<q2<<std::endl;
      if(ch >= 2)
	{
	  std::cout<<"CH error: "<<ch<<std::endl;
	  continue;
	}
      
      if(deltat > 24000) bl[ch] = q1;
      ene1 = ((q2-q1*pare[ch])/parM[ch]-bl[ch]/parM[ch]*(1-pare[ch]))*4;
      ene2 = (c0[ch]*(q1-bl[ch])+c1[ch]*(qg-bl[ch]/parM[ch]*parK[ch])+c2[ch]*(q2-bl[ch]))*(1-b1[ch])*4;

      
      if(bl[ch] > 0 && pu==0 && deltat>500)//400
	{
	  hh[ch]->Fill(ene1);
	  hh1[ch]->Fill(ene2);
	  hh2[ch]->Fill(ene1,deltat*12.5/1000);
	  hh3[ch]->Fill(ene2,deltat*12.5/1000);
	}
      
      std::cout<<((buff[0] & 0x0001))<<"  "<<ch<<"  "<<pu<<"  "<<ts_curr<<" "<<deltat<<"  "<<q1<<"  "<<qg<<"  "<<q2<<"  "<<ene1<<"  "<<ene2<<std::endl;

      if(pu==0 && deltat>500)
	{
	  df->Fill(q2-q1);
	  df2d->Fill(q2-q1,deltat*12.5/1000);
	}
      
      // for (int i = 0; i < 8; ++i)
      // 	{
      // 	  printf("%8X  ",buff[i]);
      // 	}
      // printf("\n");
    }


  
  // canvas->Divide(2,2);
  // canvas->cd(1);
  // hh[0]->Draw();
  // canvas->cd(2);
  // hh2[0]->Draw("colz");
  // canvas->cd(3);
  // hh1[0]->Draw();
  // canvas->cd(4);
  // hh3[0]->Draw("colz");


  df2d->Draw("colz");
  
  // canvas->cd(1);
  // hh[1]->Draw();
  // canvas->cd(2);
  // hh2[1]->Draw("colz");
  // canvas->cd(3);
  // hh1[1]->Draw();
  // canvas->cd(4);
  // hh3[1]->Draw("colz");

  
  // and enter the event loop...
  theApp->Run();
  delete theApp;

  return 0;
}

// 
// main.cc ends here
