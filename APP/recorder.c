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

extern u8 sequence[4096];  //�������� 4K

u8 t =0;
u8 tmp_buf[33];	 
extern vu8 wavtransferend;	//sai������ɱ�־
extern vu8 wavwitchbuf;		//saibufxָʾ��־
u8 * fifo_p;
u8 *sairecbuf1;			//SAI1 DMA����BUF1
u8 *sairecbuf2; 		//SAI1 DMA����BUF2
u8 *sairecbuf3;			//SAI1 DMA����BUF1
u8 *sairecbuf4; 		//SAI1 DMA����BUF2
u8 * recbuf;
//REC¼��FIFO�������.
//����FATFS�ļ�д��ʱ��Ĳ�ȷ����,���ֱ���ڽ����ж�����д�ļ�,���ܵ���ĳ��д��ʱ�����
//�Ӷ��������ݶ�ʧ,�ʼ���FIFO����,�Խ��������.
vu8 sairecfifordpos=0;	//FIFO��λ��
vu8 sairecfifowrpos=0;	//FIFOдλ��
u8 *sairecfifobuf[SAI_RX_FIFO_SIZE];//����10��¼������FIFO

FIL* f_rec=0;			//¼���ļ�	
u32 wavsize;			//wav���ݴ�С(�ֽ���,�������ļ�ͷ!!)
u8 rec_sta=0;			//¼��״̬
						//[7]:0,û�п���¼��;1,�Ѿ�����¼��;
						//[6:1]:����
						//[0]:0,����¼��;1,��ͣ¼��;

//��ȡ¼��FIFO
//buf:���ݻ������׵�ַ
//����ֵ:0,û�����ݿɶ�;
//      1,������1�����ݿ�
u8 rec_sai_fifo_read(u8 **buf)
{
	if(sairecfifordpos==sairecfifowrpos)return 0;
	sairecfifordpos++;		//��λ�ü�1
	if(sairecfifordpos>=SAI_RX_FIFO_SIZE)sairecfifordpos=0;//���� 
	*buf=sairecfifobuf[sairecfifordpos];
	return 1;
}
//дһ��¼��FIFO
//buf:���ݻ������׵�ַ
//����ֵ:0,д��ɹ�;
//      1,д��ʧ��
u8 rec_sai_fifo_write(u8 *buf)
{
	u16 i;
	u8 temp=sairecfifowrpos;//��¼��ǰдλ��
	sairecfifowrpos++;		//дλ�ü�1
	if(sairecfifowrpos>=SAI_RX_FIFO_SIZE)sairecfifowrpos=0;//����  
	if(sairecfifordpos==sairecfifowrpos)
	{
		sairecfifowrpos=temp;//��ԭԭ����дλ��,�˴�д��ʧ��
		return 1;	
	}
	for(i=0;i<SAI_RX_DMA_BUF_SIZE;i++)sairecfifobuf[sairecfifowrpos][i]=buf[i];//��������
	return 0;
} 

//¼�� SAI_DMA�����жϷ�����.���ж�����д������
void rec_sai_dma_rx_callback(void) 
{      
	 
    if(Get_DCahceSta())SCB_CleanInvalidateDCache();
		if(DMA2_Stream5->CR&(1<<19))rec_sai_fifo_write(sairecbuf1);	//sairecbuf1д��FIFO
		else rec_sai_fifo_write(sairecbuf2);						//sairecbuf2д��FIFO 
	 
}  
const u16 saiplaybuf[2]={0X0000,0X0000};//2��16λ����,����¼��ʱSAI Block A��������.ѭ������0.
//����PCM ¼��ģʽ 		  
void recoder_enter_rec_mode(void)
{
	WM8978_ADDA_Cfg(0,1);		//����ADC
	WM8978_Input_Cfg(1,1,0);	//��������ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(0,1);		//����BYPASS��� 
	WM8978_MIC_Gain(46);		//MIC�������� 
	WM8978_SPKvol_Set(0);		//�ر�����.
	WM8978_I2S_Cfg(2,0);		//�����ֱ�׼,16λ���ݳ���

    SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
    SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò����� 
	SAIA_TX_DMA_Init((u8*)&saiplaybuf[0],(u8*)&saiplaybuf[1],1,1);	//����TX DMA,16λ
    __HAL_DMA_DISABLE_IT(&SAI1_TXDMA_Handler,DMA_IT_TC); //�رմ�������ж�(���ﲻ���ж�������) 
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//����RX DMA
  	sai_rx_callback=rec_sai_dma_rx_callback;//��ʼ���ص�����ָsai_rx_callback
 	SAI_Play_Start();			//��ʼSAI���ݷ���(����)
	SAI_Rec_Start(); 			//��ʼSAI���ݽ���(�ӻ�)
	recoder_remindmsg_show(0);
}  
//����PCM ����ģʽ 		  
void recoder_enter_play_mode(void)
{
	WM8978_ADDA_Cfg(1,0);		//����DAC 
	WM8978_Input_Cfg(0,0,0);	//�ر�����ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//����DAC��� 
	WM8978_MIC_Gain(0);			//MIC��������Ϊ0 
	WM8978_SPKvol_Set(50);		//������������
	SAI_Play_Stop();			//ֹͣʱ�ӷ���
	SAI_Rec_Stop(); 			//ֹͣ¼��
	recoder_remindmsg_show(1);
}
//��ʼ��WAVͷ.
void recoder_wav_init(__WaveHeader* wavhead) //��ʼ��WAVͷ			   
{
	wavhead->riff.ChunkID=0X46464952;	//"RIFF"
	wavhead->riff.ChunkSize=0;			//��δȷ��,�����Ҫ����
	wavhead->riff.Format=0X45564157; 	//"WAVE"
	wavhead->fmt.ChunkID=0X20746D66; 	//"fmt "
	wavhead->fmt.ChunkSize=16; 			//��СΪ16���ֽ�
	wavhead->fmt.AudioFormat=0X01; 		//0X01,��ʾPCM;0X01,��ʾIMA ADPCM
 	wavhead->fmt.NumOfChannels=2;		//˫����
 	wavhead->fmt.SampleRate=REC_SAMPLERATE;//���ò�������
 	wavhead->fmt.ByteRate=wavhead->fmt.SampleRate*4;//�ֽ�����=������*ͨ����*(ADCλ��/8)
 	wavhead->fmt.BlockAlign=4;			//���С=ͨ����*(ADCλ��/8)
 	wavhead->fmt.BitsPerSample=16;		//16λPCM
   	wavhead->data.ChunkID=0X61746164;	//"data"
 	wavhead->data.ChunkSize=0;			//���ݴ�С,����Ҫ����  
} 						    
//��ʾ¼��ʱ�������
//tsec:������.
void recoder_msg_show(u32 tsec,u32 kbps)
{   
	//��ʾ¼��ʱ��			 
	LCD_ShowString(30,210,200,16,16,"TIME:");	  	  
	LCD_ShowxNum(30+40,210,tsec/60,2,16,0X80);	//����
	LCD_ShowChar(30+56,210,':',16,0);
	LCD_ShowxNum(30+64,210,tsec%60,2,16,0X80);	//����	
	//��ʾ����		 
	LCD_ShowString(140,210,200,16,16,"KPBS:");	  	  
	LCD_ShowxNum(140+40,210,kbps/1000,4,16,0X80);	//������ʾ 	
}  	
//��ʾ��Ϣ
//mode:0,¼��ģʽ;1,����ģʽ
void recoder_remindmsg_show(u8 mode)
{
	LCD_Fill(30,120,lcddev.width-1,180,WHITE);//���ԭ������ʾ
	POINT_COLOR=RED;
	if(mode==0)	//¼��ģʽ
	{
		Show_Str(30,120,200,16,"KEY0:REC/PAUSE",16,0); 
		Show_Str(30,140,200,16,"KEY2:STOP&SAVE",16,0); 
		Show_Str(30,160,200,16,"WK_UP:PLAY",16,0); 
	}else		//����ģʽ
	{
		Show_Str(30,120,200,16,"KEY0:STOP Play",16,0);  
		Show_Str(30,140,200,16,"WK_UP:PLAY/PAUSE",16,0); 
	}
}






void test()
{
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�1����
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�2����  
	
	recoder_enter_play_mode();

	WM8978_I2S_Cfg(2,0);	//�����ֱ�׼,16λ���ݳ���
	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò�����  
	SAIA_TX_DMA_Init(sairecbuf1,sairecbuf2,WAV_SAI_TX_DMA_BUFSIZE/2,1); //����TX DMA,16λ
	sai_tx_callback=wav_sai_dma_tx_callback;			//�ص�����ָwav_sai_dma_callback 

 	SAI_Play_Start();			//��ʼSAI���ݷ���(����)

	while(1);
}


void test2()
{
	int i;
	int pos;
	u16 ser[2]={0X0000,0X000};
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�1����
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�2����  

	sairecbuf3=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�1����
	sairecbuf4=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�2����
		
	recbuf=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�2����

	for(i=0;i<SAI_RX_FIFO_SIZE;i++)
	{
		sairecfifobuf[i]=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼��FIFO�ڴ�����
		if(sairecfifobuf[i]==NULL)break;			//����ʧ��
	}
	WM8978_ADDA_Cfg(1,1);		//����ADC
	WM8978_Input_Cfg(1,1,0);	//��������ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//����BYPASS��� 
	WM8978_MIC_Gain(46);		//MIC�������� 
	WM8978_SPKvol_Set(50);		//�ر�����.
	WM8978_HPvol_Set(45,45);	    //������������

	WM8978_I2S_Cfg(2,0);		//�����ֱ�׼,16λ���ݳ���
		
	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò�����  
	//SAIA_TX_DMA_Init((u8 *)&saiplaybuf[0],(u8 *)&saiplaybuf[1],1,1); //����TX DMA,16λ
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //����TX DMA,16λ
	sai_tx_callback=wav_sai_dma_tx_callback;			//�ص�����ָwav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//����RX DMA
	//sai_rx_callback=rec_sai_dma_rx_callback;//��ʼ���ص�����ָsai_rx_callback
	SAI_Play_Stop();
	SAI_Play_Start();			//��ʼSAI���ݷ���(����)
	SAI_Rec_Start(); 			//��ʼSAI���ݽ���(�ӻ�)
		
	NRF24L01_RX_Mode();
	
	
	while(1){
				
		
					while(wavtransferend==0);//�ȴ�wav�������; 
					wavtransferend=0;
					pos=0;
 					if(wavwitchbuf)
					{
						
							while(pos<4096){
								
							if(NRF24L01_RxPacket(tmp_buf)==0)//һ�����յ���Ϣ,����ʾ����.
							{
									for(t = 0; t < 32; t++)
									{
										sairecbuf4[pos++]=tmp_buf[t];
									}
							
							}
						}
						
						//wav_buffill(sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE,recbuf);//���buf2
							
					}
					else{
							while(pos<4096){
								
							if(NRF24L01_RxPacket(tmp_buf)==0)//һ�����յ���Ϣ,����ʾ����.
							{
									for(t = 0; t < 32; t++)
									{
										sairecbuf3[pos++]=tmp_buf[t];
									}
							
							}
						}
						//wav_buffill(sairecbuf3,WAV_SAI_TX_DMA_BUFSIZE,recbuf);//���buf1
					}			

	}
}





void buf_init()
{
	int i=0;
	sairecbuf1=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�1����
	sairecbuf2=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�2����  

	sairecbuf3=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�3����
	sairecbuf4=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼���ڴ�4����

	for(i=0;i<SAI_RX_FIFO_SIZE;i++)
	{
		sairecfifobuf[i]=mymalloc(SRAMIN,SAI_RX_DMA_BUF_SIZE);//SAI¼��FIFO�ڴ�����
	
		if(sairecfifobuf[i]==NULL)break;			//����ʧ��
	
	}
	
	
}

void wm8978_config()
{
	WM8978_ADDA_Cfg(1,1);		//����ADC
	WM8978_Input_Cfg(1,1,0);	//��������ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//����BYPASS��� 
	WM8978_MIC_Gain(46);		//MIC�������� 
	WM8978_SPKvol_Set(50);		//�ر�����.
	WM8978_HPvol_Set(45,45);	    //������������
	WM8978_I2S_Cfg(2,0);		//�����ֱ�׼,16λ���ݳ���

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò�����  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //����TX DMA,16λ
	sai_tx_callback=wav_sai_dma_tx_callback;			//�ص�����ָwav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//����RX DMA	
	sai_rx_callback=rec_sai_dma_rx_callback;			//�ص�����ָwav_sai_dma_callback 

}
void receive_mode()
{
	WM8978_ADDA_Cfg(1,0);		//����ADC
	WM8978_Input_Cfg(1,1,0);	//��������ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//����BYPASS��� 
	WM8978_MIC_Gain(0);		//MIC�������� 
	WM8978_SPKvol_Set(50);		//�ر�����.
	WM8978_HPvol_Set(45,45);	    //������������
	WM8978_I2S_Cfg(2,0);		//�����ֱ�׼,16λ���ݳ���

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò�����  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //����TX DMA,16λ
	sai_tx_callback=wav_sai_dma_tx_callback;			//�ص�����ָwav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//����RX DMA	
	//sai_rx_callback=rec_sai_dma_rx_callback;			//�ص�����ָwav_sai_dma_callback 

	
	SAI_Play_Start();			//��ʼSAI���ݷ���(����)
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

			if(NRF24L01_RxPacket(tmp_buf)==0)//һ�����յ���Ϣ,����ʾ����.
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


			if(NRF24L01_RxPacket(tmp_buf)==0)//һ�����յ���Ϣ,����ʾ����.
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
	WM8978_ADDA_Cfg(0,1);		//����ADC
	WM8978_Input_Cfg(1,1,0);	//��������ͨ��(MIC&LINE IN)
	WM8978_Output_Cfg(1,0);		//����BYPASS��� 
	WM8978_MIC_Gain(46);		//MIC�������� 
	WM8978_SPKvol_Set(0);		//�ر�����.
	WM8978_HPvol_Set(0,0);	    //������������
	WM8978_I2S_Cfg(2,0);		//�����ֱ�׼,16λ���ݳ���

	SAIA_Init(SAI_MODEMASTER_TX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_SampleRate_Set(REC_SAMPLERATE);//���ò�����  
	SAIA_TX_DMA_Init(sairecbuf3,sairecbuf4,WAV_SAI_TX_DMA_BUFSIZE/2,1); //����TX DMA,16λ
	//sai_tx_callback=wav_sai_dma_tx_callback;			//�ص�����ָwav_sai_dma_callback 
 	
	SAIB_Init(SAI_MODESLAVE_RX,SAI_CLOCKSTROBING_RISINGEDGE,SAI_DATASIZE_16);
	SAIA_RX_DMA_Init(sairecbuf1,sairecbuf2,SAI_RX_DMA_BUF_SIZE/2,1);//����RX DMA	
	sai_rx_callback=rec_sai_dma_rx_callback;			//�ص�����ָwav_sai_dma_callback 

	SAI_Play_Stop();			//��ʼSAI���ݷ���(����)
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
		for(i=0;i<WAV_SAI_TX_DMA_BUFSIZE/32;i++)//��ͣ
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



//���ɼ�������
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














