#include "recorder.h" 
#include "audioplay.h"
#include "ff.h"
#include "malloc.h"
#include "text.h"
#include "usart.h"
#include "wm8978.h"
#include "sai.h"
#include "led.h"
#include "lcd.h"
#include "delay.h"
#include "key.h"
#include "exfuns.h"  
#include "text.h"
#include "string.h"  
#include "24l01.h"

extern u8 sequence[4096];  //加密序列 4K

u8 t =0;
u8 tmp_buf[33];	 
extern vu8 wavtransferend;	//sai传输完成标志
extern vu8 wavwitchbuf;		//saibufx指示标志
u8 * fifo_p;
u8 *sairecbuf1;			//SAI1 DMA接收BUF1
u8 *sairecbuf2; 		//SAI1 DMA接收BUF2
u8 *sairecbuf3;			//SAI1 DMA接收BUF1
u8 *sairecbuf4; 		//SAI1 DMA接收BUF2
u8 * recbuf;
//REC录音FIFO管理参数.
//由于FATFS文件写入时间的不确定性,如果直接在接收中断里面写文件,可能导致某次写入时间过长
//从而引起数据丢失,故加入FIFO控制,以解决此问题.
vu8 sairecfifordpos=0;	//FIFO读位置
vu8 sairecfifowrpos=0;	//FIFO写位置
u8 *sairecfifobuf[SAI_RX_FIFO_SIZE];//定义10个录音接收FIFO

FIL* f_rec=0;			//录音文件	
u32 wavsize;			//wav数据大小(字节数,不包括文件头!!)
u8 rec_sta=0;			//录音状态
						//[7]:0,没有开启录音;1,已经开启录音;
						//[6:1]:保留
						//[0]:0,正在录音;1,暂停录音;

//读取录音FIFO
//buf:数据缓存区首地址
//返回值:0,没有数据可读;
//      1,读到了1个数据块
u8 rec_sai_fifo_read(u8 **buf)
{
	if(sairecfifordpos==sairecfifowrpos)return 0;
	sairecfifordpos++;		//读位置加1
	if(sairecfifordpos>=SAI_RX_FIFO_SIZE)sairecfifordpos=0;//归零 
	*buf=sairecfifobuf[sairecfifordpos];
	return 1;
}
//写一个录音FIFO
//buf:数据缓存区首地址
//返回值:0,写入成功;
//      1,写入失败
u8 rec_sai_fifo_write(u8 *buf)
{
	u16 i;
	u8 temp=sairecfifowrpos;//记录当前写位置
	sairecfifowrpos++;		//写位置加1
	if(sairecfifowrpos>=SAI_RX_FIFO_SIZE)sairecfifowrpos=0;//归零  
	if(sairecfifordpos==sairecfifowrpos)
	{
		sairecfifowrpos=temp;//还原原来的写位置,此次写入失败
		return 1;	
	}
	for(i=0;i<SAI_RX_DMA_BUF_SIZE;i++)sairecfifobuf[sairecfifowrpos][i]=buf[i];//拷贝数据
	return 0;
} 

//录音 SAI_DMA接收中断服务函数.在中断里面写入数据
void rec_sai_dma_rx_callback(void) 
{      
	 
    if(Get_DCahceSta())SCB_CleanInvalidateDCache();
		if(DMA2_Stream5->CR&(1<<19))rec_sai_fifo_write(sairecbuf1);	//sairecbuf1写入FIFO
		else rec_sai_fifo_write(sairecbuf2);						//sairecbuf2写入FIFO 
	 
}  
const u16 saiplaybuf[2]={0X0000,0X0000};//2个16位数据,用于录音时SAI Block A主机发送.循环发送0.
//进入PCM 录音模式 		  
void recoder_enter_rec_mode(void)
{
	WM8978_ADDA_Cfg(0,1);		//开启ADC
	WM8978_Input_Cfg(1,1,0);	//开启输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(0,1);		//开启BYPASS输出 
	WM8978_MIC_Gain(46);		//MIC增益设置 
	WM8978_SPKvol_Set(0);		//关闭喇叭.
	WM8978_I2S_Cfg(2,0);		//飞利浦标准,16位数据长度

    SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
    SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率 
	SAIA_TX_DMA_Init((u8*)&saiplaybuf[0],(u8*)&saiplaybuf[1],1,1);	//配置TX DMA,16位
    __HAL_DMA_DISABLE_IT(&SAI1_TXDMA_Handler,DMA_IT_TC); //关闭传输完成中断(这里不用中断送数据) 
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//配置RX DMA
  	sai_rx_callback=rec_sai_dma_rx_callback;//初始化回调函数指sai_rx_callback
 	SAI_Play_Start();			//开始SAI数据发送(主机)
	SAI_Rec_Start(); 			//开始SAI数据接收(从机)
	recoder_remindmsg_show(0);
}  
//进入PCM 放音模式 		  
void recoder_enter_play_mode(void)
{
	WM8978_ADDA_Cfg(1,0);		//开启DAC 
	WM8978_Input_Cfg(0,0,0);	//关闭输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//开启DAC输出 
	WM8978_MIC_Gain(0);			//MIC增益设置为0 
	WM8978_SPKvol_Set(50);		//喇叭音量设置
	SAI_Play_Stop();			//停止时钟发送
	SAI_Rec_Stop(); 			//停止录音
	recoder_remindmsg_show(1);
}
//初始化WAV头.
void recoder_wav_init(__WaveHeader* wavhead) //初始化WAV头			   
{
	wavhead->riff.ChunkID=0X46464952;	//"RIFF"
	wavhead->riff.ChunkSize=0;			//还未确定,最后需要计算
	wavhead->riff.Format=0X45564157; 	//"WAVE"
	wavhead->fmt.ChunkID=0X20746D66; 	//"fmt "
	wavhead->fmt.ChunkSize=16; 			//大小为16个字节
	wavhead->fmt.AudioFormat=0X01; 		//0X01,表示PCM;0X01,表示IMA ADPCM
 	wavhead->fmt.NumOfChannels=2;		//双声道
 	wavhead->fmt.SampleRate=REC_SAMPLERATE;//设置采样速率
 	wavhead->fmt.ByteRate=wavhead->fmt.SampleRate*4;//字节速率=采样率*通道数*(ADC位数/8)
 	wavhead->fmt.BlockAlign=4;			//块大小=通道数*(ADC位数/8)
 	wavhead->fmt.BitsPerSample=16;		//16位PCM
   	wavhead->data.ChunkID=0X61746164;	//"data"
 	wavhead->data.ChunkSize=0;			//数据大小,还需要计算  
} 						    
//显示录音时间和码率
//tsec:秒钟数.
void recoder_msg_show(u32 tsec,u32 kbps)
{   
	//显示录音时间			 
	LCD_ShowString(30,210,200,16,16,"TIME:");	  	  
	LCD_ShowxNum(30+40,210,tsec/60,2,16,0X80);	//分钟
	LCD_ShowChar(30+56,210,':',16,0);
	LCD_ShowxNum(30+64,210,tsec%60,2,16,0X80);	//秒钟	
	//显示码率		 
	LCD_ShowString(140,210,200,16,16,"KPBS:");	  	  
	LCD_ShowxNum(140+40,210,kbps/1000,4,16,0X80);	//码率显示 	
}  	
//提示信息
//mode:0,录音模式;1,放音模式
void recoder_remindmsg_show(u8 mode)
{
	LCD_Fill(30,120,lcddev.width-1,180,WHITE);//清除原来的显示
	POINT_COLOR=RED;
	if(mode==0)	//录音模式
	{
		Show_Str(30,120,200,16,"KEY0:REC/PAUSE",16,0); 
		Show_Str(30,140,200,16,"KEY2:STOP&SAVE",16,0); 
		Show_Str(30,160,200,16,"WK_UP:PLAY",16,0); 
	}else		//放音模式
	{
		Show_Str(30,120,200,16,"KEY0:STOP Play",16,0);  
		Show_Str(30,140,200,16,"WK_UP:PLAY/PAUSE",16,0); 
	}
}






void test()
{
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存1申请
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存2申请  
	
	recoder_enter_play_mode();

	WM8978_I2S_Cfg(2,0);	//飞利浦标准,16位数据长度
	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率  
	SAIA_TX_DMA_Init(sairecbuf1,sairecbuf2,WAV_SAI_TX_DMA_BUFSIZE/2,1); //配置TX DMA,16位
	sai_tx_callback=wav_sai_dma_tx_callback;			//回调函数指wav_sai_dma_callback 

 	SAI_Play_Start();			//开始SAI数据发送(主机)

	while(1);
}


void test2()
{
	int i;
	int pos;
	u16 ser[2]={0X0000,0X000};
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存1申请
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存2申请  

	sairecbuf3=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存1申请
	sairecbuf4=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存2申请
		
	recbuf=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存2申请

	for(i=0;i<SAI_RX_FIFO_SIZE;i++)
	{
		sairecfifobuf[i]=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音FIFO内存申请
		if(sairecfifobuf[i]==NULL)break;			//申请失败
	}
	WM8978_ADDA_Cfg(1,1);		//开启ADC
	WM8978_Input_Cfg(1,1,0);	//开启输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//开启BYPASS输出 
	WM8978_MIC_Gain(46);		//MIC增益设置 
	WM8978_SPKvol_Set(50);		//关闭喇叭.
	WM8978_HPvol_Set(45,45);	    //耳机音量设置

	WM8978_I2S_Cfg(2,0);		//飞利浦标准,16位数据长度
		
	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率  
	//SAIA_TX_DMA_Init((u8 *)&saiplaybuf[0],(u8 *)&saiplaybuf[1],1,1); //配置TX DMA,16位
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //配置TX DMA,16位
	sai_tx_callback=wav_sai_dma_tx_callback;			//回调函数指wav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//配置RX DMA
	//sai_rx_callback=rec_sai_dma_rx_callback;//初始化回调函数指sai_rx_callback
	SAI_Play_Stop();
	SAI_Play_Start();			//开始SAI数据发送(主机)
	SAI_Rec_Start(); 			//开始SAI数据接收(从机)
		
	NRF24L01_RX_Mode();
	
	
	while(1){
				
		
					while(wavtransferend==0);//等待wav传输完成; 
					wavtransferend=0;
					pos=0;
 					if(wavwitchbuf)
					{
						
							while(pos<4096){
								
							if(NRF24L01_RxPacket(tmp_buf)==0)//一旦接收到信息,则显示出来.
							{
									for(t = 0; t < 32; t++)
									{
										sairecbuf4[pos++]=tmp_buf[t];
									}
							
							}
						}
						
						//wav_buffill(sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE,recbuf);//填充buf2
							
					}
					else{
							while(pos<4096){
								
							if(NRF24L01_RxPacket(tmp_buf)==0)//一旦接收到信息,则显示出来.
							{
									for(t = 0; t < 32; t++)
									{
										sairecbuf3[pos++]=tmp_buf[t];
									}
							
							}
						}
						//wav_buffill(sairecbuf3,WAV_SAI_TX_DMA_BUFSIZE,recbuf);//填充buf1
					}			

	}
}





void buf_init()
{
	int i=0;
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存1申请
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存2申请  

	sairecbuf3=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存3申请
	sairecbuf4=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音内存4申请

	for(i=0;i<SAI_RX_FIFO_SIZE;i++)
	{
		sairecfifobuf[i]=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI录音FIFO内存申请
	
		if(sairecfifobuf[i]==NULL)break;			//申请失败
	
	}
	
	
}

void wm8978_config()
{
	WM8978_ADDA_Cfg(1,1);		//开启ADC
	WM8978_Input_Cfg(1,1,0);	//开启输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//开启BYPASS输出 
	WM8978_MIC_Gain(46);		//MIC增益设置 
	WM8978_SPKvol_Set(50);		//关闭喇叭.
	WM8978_HPvol_Set(45,45);	    //耳机音量设置
	WM8978_I2S_Cfg(2,0);		//飞利浦标准,16位数据长度

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //配置TX DMA,16位
	sai_tx_callback=wav_sai_dma_tx_callback;			//回调函数指wav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//配置RX DMA	
	sai_rx_callback=rec_sai_dma_rx_callback;			//回调函数指wav_sai_dma_callback 

}
void receive_mode()
{
	WM8978_ADDA_Cfg(1,0);		//开启ADC
	WM8978_Input_Cfg(1,1,0);	//开启输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//开启BYPASS输出 
	WM8978_MIC_Gain(0);		//MIC增益设置 
	WM8978_SPKvol_Set(50);		//关闭喇叭.
	WM8978_HPvol_Set(45,45);	    //耳机音量设置
	WM8978_I2S_Cfg(2,0);		//飞利浦标准,16位数据长度

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //配置TX DMA,16位
	sai_tx_callback=wav_sai_dma_tx_callback;			//回调函数指wav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//配置RX DMA	
	//sai_rx_callback=rec_sai_dma_rx_callback;			//回调函数指wav_sai_dma_callback 

	
	SAI_Play_Start();			//开始SAI数据发送(主机)
	SAI_Rec_Stop(); 			
		
	NRF24L01_RX_Mode();
	
}

void rec_data(int code)
{
	
	static int t=0,pos=0,decode_pos=0;
	while(wavtransferend==0);
	wavtransferend=0;
	pos=0;
	if(wavwitchbuf)
	{
		while(pos<4096){

			if(NRF24L01_RxPacket(tmp_buf)==0)//一旦接收到信息,则显示出来.
			{
				
				for(t = 0; t < 32; t++)
				{
					
					
					
				//	sairecbuf4[pos]=tmp_buf[t]^sequence[	decode_pos*32 + t];
					//pos++;
					
					
						if(code == 1&&tmp_buf[0]==0X00){
							
						sairecbuf4[pos++]=tmp_buf[t];

						

					}
					else if(code==0&&tmp_buf[0]!=0x00)
					{
							sairecbuf4[pos++]=tmp_buf[t];

					}
					else{
								//decode_pos = tmp_buf[0];
								sairecbuf4[pos++] ^=sequence[	pos ];
					}
				}
		//				sairecbuf4[decode_pos*32] = 0;

			}

		}

	}
	else
	{
		while(pos<4096)
		{

			decode_pos=tmp_buf[0];


			if(NRF24L01_RxPacket(tmp_buf)==0)//一旦接收到信息,则显示出来.
			{
				for(t = 0; t < 32; t++)
				{
					
//					sairecbuf3[pos]=tmp_buf[t]^sequence[	decode_pos*32 + t];
//					pos++;
					
						
					if(code == 1&&tmp_buf[0]==0X00){
							
						sairecbuf3[pos++]=tmp_buf[t];

						

					}
					else if(code==0&&tmp_buf[0]!=0x00)
					{
							sairecbuf3[pos++]=tmp_buf[t];

					}
					else{
								sairecbuf3[pos++] ^=sequence[	pos ];
					}
				}
				
		//				sairecbuf3[decode_pos*32] = 0;

			}
		
		}
	}			

}

void send_mode()
{	
	WM8978_ADDA_Cfg(0,1);		//开启ADC
	WM8978_Input_Cfg(1,1,0);	//开启输入通道(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//开启BYPASS输出 
	WM8978_MIC_Gain(46);		//MIC增益设置 
	WM8978_SPKvol_Set(0);		//关闭喇叭.
	WM8978_HPvol_Set(0,0);	    //耳机音量设置
	WM8978_I2S_Cfg(2,0);		//飞利浦标准,16位数据长度

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//设置采样率  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //配置TX DMA,16位
	//sai_tx_callback=wav_sai_dma_tx_callback;			//回调函数指wav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//配置RX DMA	
	sai_rx_callback=rec_sai_dma_rx_callback;			//回调函数指wav_sai_dma_callback 

	SAI_Play_Stop();			//开始SAI数据发送(主机)
	SAI_Rec_Start(); 			
		
	NRF24L01_TX_Mode();	
}


void send_data(int code)
{	
	static u8 * fifo_p,*temp_p;
	int i = 0,j=0;
	if( rec_sai_fifo_read(&fifo_p) )
	{
		temp_p=fifo_p;
		for(i=0;i<WAV_SAI_TX_DMA_BUFSIZE/32;i++)//暂停
		{
			if(code==1){
				
				for(j=0;j<32;j++)
				{
							temp_p[i*32+j] =temp_p[i*32+j] ^ sequence[i*32+j];

						//	temp_p[i*32+j] ^= sequence[i*32+j];

				}
				
				temp_p[i*32]=0x00;
			}

			NRF24L01_TxPacket(fifo_p);
			fifo_p+=32;

		}
	}

	delay_us(20);

}



//生成加密序列
void generate_logistic(u8 * result)
{
	float y1=0,x1=0.7,u1=3.8; 

	float y2=0,x2=0.9,u2=3.9; 

	int i = 0;
	
	int pos = 0;

	for(i=1;i<4096*8;i++)
	{
		y1 = u1 * x1 * ( 1 - x1 );

		y2 = u2 * x2 * ( 1 - x2 );

		x1=y1;
		
		x2=y2;
		
		if(y1>y2)
		{
			result[i/8] |= 1 << (i%8);	
		}

	}
}














