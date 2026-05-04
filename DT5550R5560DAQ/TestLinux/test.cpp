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
#include <fstream>
#include <iostream>

int main(){
    uint32_t data[1024];
    uint32_t datar[2048];
    uint32_t written_data;
    uint32_t read_data;
    uint32_t valid_data;   
    char board_model[32];   
    uint32_t data1;  
    NI_HANDLE dt5550;


	//Replace NI120010 with the serial number of the board.
	//You can see it during firmware upgrade
    if (NI_USB3_ConnectDevice("NI110011", &dt5550)) {
        printf("Unable to connect to the board");
        exit(0);
    }

	NI_USB3_ReadReg(&data1, 0xFFFEFFFF, &dt5550);
	printf("ip identifier: %8x\n", data1); 
	//you shoould read 0x1234ABBA
	NI_USB3_ReadReg(&data1, 0xFFFFFFFE, &dt5550);
	printf("ip version: %8x\n", data1); 
	//you shoould read 0x21022100
	NI_USB3_ReadReg(&data1, 0xFFFFFFFF, &dt5550);
	printf("board bodel: %8x\n", data1); 
	//you shoould read 0x00555000

	for (int i = 0; i < 1024; ++i)
	  {
	    data[i] = i;
	  }



	
	NI_USB3_WriteData(data, 
			  1024,
			  0xFFFD0000,
			  REG_ACCESS,
			  1000, &dt5550, 
			  &written_data);

	std::ofstream datawrite;
	datawrite.open("data.dat");
	if(!datawrite.is_open())
	  {
	    std::cout<<"can't open file."<<std::endl;
	  }
	
	NI_USB3_ReadData(datar,
			 1024,
			 0xFFFD0000,
			 REG_ACCESS ,// STREAMING  
			 1000, &dt5550,  
			 &read_data, 
			 &valid_data);


	for (int i=0;i<1024;i++)
		if (data[i]!=datar[i])
		  printf("%d   :   %d\n", i, datar[i]);


	datawrite<<datar[0]<<" "<<datar[1]<<" "<<datar[2]<<" "<<datar[3]<<" "<<datar[4]<<std::endl;
	datawrite.close();


	
    printf("\n\n");
}
