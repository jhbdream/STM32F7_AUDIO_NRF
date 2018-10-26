#include "wavplay.h" 
#include "audioplay.h"
#include "usart.h" 
#include "delay.h" 
#include "malloc.h"
#include "ff.h"
#include "sai.h"
#include "wm8978.h"
#include "key.h"
#include "led.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32开发板
//WAV 解码代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2016/1/18
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved				
//********************************************************************************
//V1.0 说明
//1,支持16位/24位WAV文件播放
//2,最高可以支持到192K/24bit的WAV格式. 
////////////////////////////////////////////////////////////////////////////////// 	
   
extern u8 *sairecbuf1;			//SAI1 DMA接收BUF1
extern u8 *sairecbuf2; 		//SAI1 DMA接收BUF2
extern u8 *sairecbuf3;			//SAI1 DMA接收BUF1
extern u8 *sairecbuf4; 		//SAI1 DMA接收BUF2
__wavctrl wavctrl;		//WAV控制结构体
vu8 wavtransferend=0;	//sai传输完成标志
vu8 wavwitchbuf=0;		//saibufx指示标志

u8 * dat;

 //WAV播放时,SAI DMA传输回调函数
void wav_sai_dma_tx_callback(void) 
{   
	u16 i;
	if(DMA2_Stream3->CR&(1<<19))
	{
		wavwitchbuf=0;
		
	}else 
	{
		wavwitchbuf=1;
	
	}
	wavtransferend=1;
} 