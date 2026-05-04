// cpack.cpp --- 
// 
// Description: 
// Author: Hongyi Wu(吴鸿毅)
// Email: wuhongyi@qq.com 
// Created: 三 12月 29 16:32:03 2021 (+0800)
// Last-Updated: 三 12月 29 18:52:33 2021 (+0800)
//           By: Hongyi Wu(吴鸿毅)
//     Update #: 23
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
// #ifdef DT5550
// #include "ni_usb3_wrapper.h"
// #endif  
// #ifdef R5560
#include "R5560_SDKLib.h"
// #endif  
#include "RegisterFile.h"
#include "circular_buffer.h"
#include "wuReadData.hh"
#include <iostream>
//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

typedef struct  {
    uint32_t row[4];
}t_CP_0_struct;


typedef struct
{
	uint64_t Time_Code;
	uint32_t Id;
	uint32_t Pack_Id;
	uint32_t *Energy;
	uint32_t Valid;
} t_FRAME_packet;


typedef struct
{
	t_FRAME_packet *packets;
	int allocated_packets;
	int valid_packets;
} t_FRAME_packet_collection;



typedef struct
{
	int board;
	uint64_t timestamp;
	uint64_t triggerid;
	void *payload;
}t_generic_event;

typedef struct
{
	t_generic_event *packets;
	int allocated_packets;
	int valid_packets;
} t_generic_event_collection;


void free_packet_collection (t_generic_event_collection *decoded_packets)
{
  int i;
  for (i = 0; i < decoded_packets->allocated_packets; i++)
    {
      free(decoded_packets->packets[i].payload);
    }
  free(decoded_packets->packets);
}


// This function take as input a pointer to a buffer handle (not pre-allocated void*) and fill it with the pointer to the circular buffer to store data acquired from a board FIFO that is waiting to be processed.
int Utility_ALLOCATE_DOWNLOAD_BUFFER(void **buffer_handle, uint32_t buffer_size)
{
  uint32_t * buffer = malloc(buffer_size * sizeof(uint32_t));
  if (buffer == NULL) return -1;
  cbuf_handle_t cbuf = circular_buf_init(buffer, buffer_size);
  *buffer_handle = cbuf;
  return 0;
}

// This function take as input a pointer to a buffer handle allocated with the function 
// Utility_ALLOCATE_DOWNLOAD_BUFFER and fill it with the content of the array val
// If the buffer is full the function fails and the output parameter 
// The buffer_size specify the size of the buffer to be allocated enqueued_data willl contain the number of word that has been enqueued in the buffer
int Utility_ENQUEUE_DATA_IN_DOWNLOAD_BUFFER(void *buffer_handle, int32_t *val, uint32_t size, uint32_t *enqueued_data)
{
  cbuf_handle_t cbuf;
  uint32_t i;
  uint32_t data_counter=0;
  cbuf = ((cbuf_handle_t)buffer_handle);
  for (i = 0; i < size; i++)
    {
      if (circular_buf_full(cbuf))
	{
	  *enqueued_data = data_counter;
	  return -1;
	}
      data_counter++;
      circular_buf_put(cbuf, val[i]);
    }
  *enqueued_data = data_counter;
  return 0;
}




int CPACK_CP_0_RECONSTRUCT_DATA(void *buffer_handle, t_generic_event_collection *decoded_packets)
{
  cbuf_handle_t cbuf;
  cbuf = (cbuf_handle_t)buffer_handle;
  int PacketSize =5;
  int in_sync = 0;
  uint64_t event_timecode = 0;
  uint32_t ev_energy = 0;
  uint32_t mpe = 0;
  int ch_index = 0;
  int i = 0,j;
  int k = 0;
  decoded_packets->packets = NULL;
  decoded_packets->allocated_packets = 0;
  decoded_packets->valid_packets = 0;
	
  //check if we have elements in the circular buffer
  int bfsize = circular_buf_size(cbuf);
  if (bfsize < PacketSize + 1) return -1;
  //allocate output
  int possible_packets = (circular_buf_size(cbuf) / PacketSize)+10;
  if (possible_packets < 1) return -1;
  decoded_packets->packets = (t_generic_event *)malloc(possible_packets * sizeof(t_generic_event));
  if (decoded_packets->packets==NULL) return -2;
  decoded_packets->allocated_packets = possible_packets;
  decoded_packets->valid_packets = 0;
  t_CP_0_struct temp_data;
  //process packets
  while (circular_buf_size(cbuf)> 0)
    {
      circular_buf_get(cbuf, &mpe);

      if (in_sync == 0) {
	if (mpe != 0x80000000)
	  {
	    continue;
	  }
	in_sync = 1;
	ch_index =0;
	continue;
      }
      if (in_sync == 1) {
	temp_data.row[0]  =  mpe;
	in_sync = 2;
	continue;
      }
      if (in_sync == 2) {
	temp_data.row[1]  =  mpe;
	in_sync = 3;
	continue;
      }
      if (in_sync == 3) {
	temp_data.row[2]  =  mpe;
	in_sync = 4;
	continue;
      }
      if (in_sync == 4) {
	temp_data.row[3]  =  mpe;
	decoded_packets->packets[k].payload = malloc(sizeof(t_CP_0_struct));
	if (decoded_packets->packets[k].payload != NULL) {
	  *((t_CP_0_struct*)decoded_packets->packets[k].payload) = temp_data;
	  k++;
	  decoded_packets->valid_packets = k;
	}
	if (k > decoded_packets->allocated_packets) return 0;
			
	in_sync = 0;
	if (circular_buf_size(cbuf) >= PacketSize)
	  continue;
	else
	  break;
      }
    }

  return 0;

}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......



int main(int argc, char *argv[])
{
  int err_code = 0;
  tR5560_Handle handle;
  int ret;
  err_code = R5560_ConnectTCP(IPADDR, 8888, &handle);
  if (err_code != 0)
    {
      printf("Unable to connect to: %s - %d\n", IPADDR, err_code);
      return 0;
    }

  uint32_t status_frame = 0;//
  uint32_t N_Packet = 100;//
  uint32_t data_frame[100000];//
  uint32_t read_data_frame;//
  uint32_t valid_data_frame;//
  uint32_t valid_data_enqueued;//

  uint32_t N_Total_Events = 10000;
  uint32_t ReadDataNumber = 0;
  int32_t timeout_frame = 1000;//
  t_generic_event_collection decoded_packets;

  //Configuration flag
  int32_t FrameSync = 0;
  int32_t	FrameWait = 0;
  int32_t	FrameMask = 3;
  int32_t	FrameExternalTrigger = 0;
  int32_t	FrameOrTrigger = 1;

  void *BufferDownloadHandler = NULL;
  Utility_ALLOCATE_DOWNLOAD_BUFFER(&BufferDownloadHandler, 1024*1024);
  printf("%x\n", BufferDownloadHandler);


  // reset
  ret = NI_WriteReg(0,SCI_REG_CP_0_CONFIG, &handle);
  if(ret!=0) printf("CP RESET: %d\n",ret);
  // start
  ret = NI_WriteReg(1,SCI_REG_CP_0_CONFIG, &handle);
  if(ret!=0) printf("CP START: %d\n",ret);
  // stop
  // ret = NI_WriteReg(0,SCI_REG_CP_0_CONFIG, &handle);
  // if(ret!=0) printf("CP STOP: %d\n",ret);


  
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
      // if(countlist % 100 == 0)
	std::cout<<"countlist: "<<ReadDataNumber<<std::endl;

      // status
      ret = NI_ReadReg(&status_frame, SCI_REG_CP_0_READ_STATUS,&handle);
      status_frame = status_frame & 0xF;
      if(ret!=0) printf("CP STATUS: %d\n",ret);

      if(status_frame > 0)
	{
	  valid_data_frame = 0;
	  bool blocking = 1;

	  ret = NI_ReadFifo(data_frame, N_Packet * 5, SCI_REG_CP_0_FIFOADDRESS,  SCI_REG_CP_0_READ_STATUS, blocking ? STREAMING_BLOCKING : STREAMING_NONBLOCKING, timeout_frame, &handle, &read_data_frame);
	  // for (int j = 0; j < read_data_frame; ++j)
	  //   {
	  //     printf("%03d  %08X\n",j, data_frame[j]);
	  //   }
	  valid_data_frame = read_data_frame;
	  if(ret!=0) printf("CP READDATA: %d\n",ret);

	  valid_data_enqueued = 0;
	  Utility_ENQUEUE_DATA_IN_DOWNLOAD_BUFFER(BufferDownloadHandler, data_frame, valid_data_frame, &valid_data_enqueued);

	  if (CPACK_CP_0_RECONSTRUCT_DATA(BufferDownloadHandler, &decoded_packets) == 0)
	    {
	      // printf(".");
	      for (int i =0; i<decoded_packets.valid_packets; i++){
		t_CP_0_struct *data = decoded_packets.packets[i].payload;
		// printf("%x\n", data->row[0]);
	      }
	      // free_packet_collection(&decoded_packets);
	    }
	  ReadDataNumber = ReadDataNumber+ N_Packet;

	  
	  // countlist += word_list/size_list;
	  if(ReadDataNumber >= N_Total_Events) break;
	}
    }

  fclose(fsave);
  
  return 0;
}

/* //REMOVE THIS COMMENT TO ENABLE THE EXAMPLE CODE

	uint32_t status_frame = 0;
	uint32_t N_Packet = 100;
	uint32_t data_frame[100000];
	uint32_t read_data_frame;
	uint32_t valid_data_frame;
	uint32_t valid_data_enqueued;

	uint32_t N_Total_Events = 10000;
	uint32_t ReadDataNumber = 0;
	int32_t timeout_frame = 1000;
	t_generic_event_collection decoded_packets;

	//Configuration flag
	int32_t FrameSync = 0;
	int32_t	FrameWait = 0;
	int32_t	FrameMask = 3;
	int32_t	FrameExternalTrigger = 0;
	int32_t	FrameOrTrigger = 1;

	void *BufferDownloadHandler = NULL;
	Utility_ALLOCATE_DOWNLOAD_BUFFER(&BufferDownloadHandler, 1024*1024);
	printf("%x\n", BufferDownloadHandler);

	if (CPACK_CP_0_RESET(&handle) != 0) printf("Reset Error");
	if (CPACK_CP_0_START(&handle) != 0) printf("Start Error");
	if (CPACK_CP_0_STATUS(&status_frame, &handle) != 0) printf("Status Error");
	if (status_frame >0)
	{
		while (1)
		{
			valid_data_frame = 0;
			if (CPACK_CP_0_DOWNLOAD(&data_frame, N_Packet * (<<<PACKET_SIZE_HERE_IN_WORK>>>), timeout_frame, &handle, &read_data_frame, &valid_data_frame) != 0) printf("Data Download Error");
			
			valid_data_enqueued = 0;
			Utility_ENQUEUE_DATA_IN_DOWNLOAD_BUFFER(BufferDownloadHandler, data_frame, valid_data_frame, &valid_data_enqueued);

			if (CPACK_CP_0_RECONSTRUCT_DATA(BufferDownloadHandler, &decoded_packets) == 0)
			{
				printf(".");
				for (int i =0;i<decoded_packets.valid_packets;i++){
				    t_CP_0_struct *data = decoded_packets.packets[i].payload;
				    printf("%d\n", data->row[0]);
				}
				free_packet_collection(&decoded_packets);
			}
			ReadDataNumber = ReadDataNumber+ N_Packet;
	}
		printf("Download completed");
	}
	else printf("Status Error");

*/


// 
// cpack.cpp ends here
