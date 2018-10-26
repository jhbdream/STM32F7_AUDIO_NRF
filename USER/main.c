#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "key.h"
#include "lcd.h"
#include "ltdc.h"
#include "sdram.h"
#include "w25qxx.h"
#include "nand.h"  
#include "sdmmc_sdcard.h"
#include "malloc.h"
#include "ff.h"
#include "exfuns.h"
#include "fontupd.h"
#include "text.h"
#include "wm8978.h"	 
#include "audioplay.h"
#include "recorder.h"
#include "24l01.h"
u8 sequence[4096];  //加密序列 4K

int main(void)
{ 
	u8 key,mode;
	u32 t;
    Cache_Enable();                 //打开L1-Cache
    HAL_Init();				        //初始化HAL库
    Stm32_Clock_Init(432,25,2,9);   //设置时钟,216Mhz 
    delay_init(216);                //延时初始化
		uart_init(115200);		        //串口初始化
    LED_Init();                     //初始化LED
    KEY_Init();                     //初始化按键
    SDRAM_Init();                   //初始化SDRAM
    LCD_Init();                     //初始化LCD
		W25QXX_Init();				    //初始化W25Q256
    WM8978_Init();				    //初始化WM8978
		WM8978_HPvol_Set(40,40);	    //耳机音量设置
		WM8978_SPKvol_Set(40);		    //喇叭音量设置
    my_mem_init(SRAMIN);            //初始化内部内存池
    my_mem_init(SRAMEX);            //初始化外部SDRAM内存池
    my_mem_init(SRAMDTCM);          //初始化内部DTCM内存池
   
		NRF24L01_Init();    		    //初始化NRF24L01 
		while(NRF24L01_Check());

		POINT_COLOR=BLACK;      
		buf_init();
		wm8978_config();
			
		generate_logistic(sequence);

		LCD_ShowString(30,50,200,16,32,"Voice transmission"); 
		
		while(1)
		{
			key=KEY_Scan(0);
			if(key==KEY0_PRES)
			{
				mode=0;   
				break;
			}else if(key==KEY1_PRES)
			{
				mode=1;
				break;
			}
			t++;
			if(t==120)LCD_ShowString(10,150,420,32,32,"KEY0:RX_Mode  KEY1:TX_Mode"); //闪烁显示提示信息
			if(t==240)
			{	
				LCD_Fill(10,150,430,150+32,WHITE);
				t=0; 
			}
			delay_ms(5);	 
			
		}
 
		LCD_Fill(10,150,430,182,WHITE);//清空上面的显示		  
		POINT_COLOR=BLUE;//设置字体为蓝色
					
		LCD_ShowString(30,200,400,32,32,"PRESS KEY0:NO Encryption");
		LCD_ShowString(30,232,400,32,32,"PRESS KEY1:Encryption");

		if(mode==0)
		{
					
			
			LCD_ShowString(30,150,400,32,32,"Voice RX_Mode");	

			receive_mode();
		 
			while(1) 
			{
				key=KEY_Scan(0);
				if(key==KEY0_PRES)
				{

				LCD_Fill(30,200,430,300,WHITE);//清空上面的显示		  

					LCD_ShowString(30,200,400,100,32,"NO encryption");	

					while(1) rec_data(0);
				}
				
				else if(key==KEY1_PRES)
				{
				LCD_Fill(30,200,430,300,WHITE);//清空上面的显示		  

					LCD_ShowString(30,200,400,100,32,"encryption");	

					while(1) rec_data(1);
				}
				
			}
		}
	  
		else
		{
									

				LCD_ShowString(30,150,400,32,32,"Voice TX_Mode");	

				send_mode();
		 
			while(1) 
			{
				key=KEY_Scan(0);
				if(key==KEY0_PRES)
				{
											LCD_Fill(30,200,430,300,WHITE);//清空上面的显示		  
		
					LCD_ShowString(30,200,400,100,32,"NO encryption");	

						while(1) send_data(0);
				}			
				else if(key==KEY1_PRES)
				{
									LCD_Fill(30,200,430,300,WHITE);//清空上面的显示		  

									
					LCD_ShowString(30,200,400,100,32,"encryption");	

						while(1) send_data(1);
				}		
			}
			//	while(1) send_data();
		
		}
}
