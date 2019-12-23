#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hps_soc_system.h"
#include "socal.h"
#include "hps.h"

#define KEY_BASE              0xFF200050
#define VIDEO_IN_BASE         0xFF203060
#define FPGA_ONCHIP_BASE      0xC8000000
#define FPGA_CHAR_BASE        0xC9000000

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

//Anamitra Datta, Rishabh Singhvi, Zach Toscanini
//Lab 4 

void Key_interrupt(){
	
	volatile int * KEY_ptr = (int *) KEY_BASE;
	
	while (1)
	{
		if (*KEY_ptr != 0)						// check if any KEY was pressed
		{
			while (*KEY_ptr != 0);	
			break;
		}
	}

}

int main(void)
{
	volatile int * KEY_ptr				= (int *) KEY_BASE;
	volatile int * Video_In_DMA_ptr	= (int *) VIDEO_IN_BASE;
	volatile short * Video_Mem_ptr	= (short *) FPGA_ONCHIP_BASE;

	int x, y;
	int counter =0;
	short threshold =0;
	
	while(1){
		*(Video_In_DMA_ptr + 3)	= 0x4;				// Enable the video
		
		while (1)
		{
			if (*KEY_ptr != 0)						// check if any KEY was pressed
			{
				*(Video_In_DMA_ptr + 3) = 0x0;			// Disable the video to capture one frame
				counter++;		
				while (*KEY_ptr != 0);				// wait for pushbutton KEY release
				break;
			}
		}
		
		int loop_count =0;				
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 320; x++) {
				short temp2 = *(Video_Mem_ptr + (y << 9) + x);
				if ((x==0) && (y==0)) { 
					threshold=temp2;
				}
				threshold = (loop_count*threshold + temp2)/(loop_count+1);
				loop_count++;

				*(Video_Mem_ptr + (y << 9) + x) = temp2;
			}
		}
		 
		Key_interrupt();
		
		//--------------black and white------------
		
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 320; x++) {
				short temp2 = *(Video_Mem_ptr + (y << 9) + x);
				if (temp2 <threshold){
					*(Video_Mem_ptr + (y << 9) + x) = 0xFFFF;
				}
				else{
					*(Video_Mem_ptr + (y << 9) + x) = 0x0000;
				}
			}
		}
		
		Key_interrupt();
		
		// convert picture to a bit array
		unsigned char bitArray[9600]; // 1 bit for every pixel for 320x240 video buffer
    	int bitCount=0;
    	unsigned int index=0;
    	unsigned char pixelByte=0;
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 320; x++) {
 				unsigned short temp = *(Video_Mem_ptr + (y << 9) + x);
				if (temp==0x0000){
					pixelByte = pixelByte>>1; //shift by 1 bit . left most bit zero
				}
				else{
					pixelByte = pixelByte>>1 | 0x80; //shift by 1 bit . set left most bit as 1
				}
				
           		if (bitCount ==7){
               		bitCount=0;
               		bitArray[index]= pixelByte; 
               		index++;
               		pixelByte=0;
          		}
				else{
					bitCount++;
				}
        	}
    	}  
		
		// remove picture from screen ..wait for decompressed picture
		*(Video_In_DMA_ptr + 3)	= 0x0;  //disable video
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 320; x++) {
				*(Video_Mem_ptr + (y << 9) + x) = 0x0;

			} 
		}
		
		Key_interrupt();
		
		// start compression by inputing bitArray into RLE and decompression from output FIFO
		
    	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 1);
    	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 0);
    	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,0);
     	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,0);
     	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RESULT_READY_PIO_BASE,1);
     	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_RESET_BASE,1);
     	alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_RESET_BASE,0);
		
		short temp3[320][240];
		int x_coord = 0;
		int y_coord = 0;
		double encoding_count =0;
		int i =0;
		
		while (i <9600){
			if ((alt_read_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_FULL_PIO_BASE)&0x1)==0){  // check if input fifo empty
				alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,1); //assert to write unencoded image data to input FIFO
				alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+ODATA_PIO_BASE, bitArray[i]); // put a byte in input array
				i++;
				alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_IN_WRITE_REQ_PIO_BASE,0);
			}
			if((alt_read_byte(RESULT_READY_PIO_BASE+ALT_FPGA_BRIDGE_LWH2F_OFST)&0x1)==0){  // not sure if should be 1 or 0 . this is a flag for empty fifo so 0 might work
				encoding_count++;
				alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,1); //enable write signal
				unsigned int data = alt_read_word(ALT_FPGA_BRIDGE_LWH2F_OFST+IDATA_PIO_BASE);
				unsigned int cdata = data;
				short bitVal = (cdata >> 23) & 1;
				int count = data & 0x007FFFFF;
			
				// start decompression and make the video array in temp3[320[240]
				int u=0;
				for (u=0;u<count;u++){
					temp3[x_coord][y_coord]= bitVal;
					x_coord++;
					if (x_coord==320){
						x_coord=0;
						y_coord++;
					}
				}
				alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+FIFO_OUT_READ_REQ_PIO_BASE,0);     //disable write signal                   
        	}
		}  // end while 
		
		alt_write_byte(ALT_FPGA_BRIDGE_LWH2F_OFST+RLE_FLUSH_PIO_BASE, 1); 
	
		// display the decompressed image from temp3 to video buffer
		for (y = 0; y < 240; y++) {
			for (x = 0; x < 320; x++) {
				if (temp3[x][y] == 1)
					*(Video_Mem_ptr + (y << 9) + x) = 0xFFFF;
				else
					*(Video_Mem_ptr + (y << 9) + x) = 0x0000;
			}
		}
		
		double rle_compression_ratio = (9600/(encoding_count*3));
		double app_compression_ratio = (9600/(encoding_count*4));
		printf("RLE Compression Ratio= %lf\n", rle_compression_ratio);
		printf("Application Compression Ratio= %lf\n", app_compression_ratio);
		Key_interrupt();	
	}
		
	return 0;
}