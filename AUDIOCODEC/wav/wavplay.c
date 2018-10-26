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
//������ֻ��ѧϰʹ�ã�δ��������ɣ��������������κ���;
//ALIENTEK STM32������
//WAV �������	   
//����ԭ��@ALIENTEK
//������̳:www.openedv.com
//��������:2016/1/18
//�汾��V1.0
//��Ȩ���У�����ؾ���
//Copyright(C) ������������ӿƼ����޹�˾ 2014-2024
//All rights reserved				
//********************************************************************************
//V1.0 ˵��
//1,֧��16λ/24λWAV�ļ�����
//2,��߿���֧�ֵ�192K/24bit��WAV��ʽ. 
////////////////////////////////////////////////////////////////////////////////// 	
   
extern u8 *sairecbuf1;			//SAI1 DMA����BUF1
extern u8 *sairecbuf2; 		//SAI1 DMA����BUF2
extern u8 *sairecbuf3;			//SAI1 DMA����BUF1
extern u8 *sairecbuf4; 		//SAI1 DMA����BUF2
__wavctrl wavctrl;		//WAV���ƽṹ��
vu8 wavtransferend=0;	//sai������ɱ�־
vu8 wavwitchbuf=0;		//saibufxָʾ��־

u8 * dat;

 //WAV����ʱ,SAI DMA����ص�����
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