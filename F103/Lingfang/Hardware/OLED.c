#include "stm32f10x.h"
#include "OLED_Font.h"
#include "Delay.h"
#include <math.h>

/*引脚配置*/
#define OLED_W_SCL(x)		GPIO_WriteBit(GPIOB, GPIO_Pin_6, (BitAction)(x))
#define OLED_W_SDA(x)		GPIO_WriteBit(GPIOB, GPIO_Pin_7, (BitAction)(x))
#define PI 3.1415926
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define CONNECTLINES1 4





/*引脚初始化*/
void OLED_I2C_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_W_SDA(0);
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(!!(Byte & (0x80 >> i)));
		OLED_W_SCL(1);
		OLED_W_SCL(0);
	}
	OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command); 
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x40);		//写数据
	OLED_I2C_SendByte(Data);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}



/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{  
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}


//显示玩家
void OLED_Showplayer(uint8_t Line, uint8_t Column, char Char)
{      	
	uint8_t i;
	OLED_SetCursor((Line - 1) , (Column - 1) * 8);		//设置光标位置在上半部分

	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_TU[Char-1][i]);			
	}
}



/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{      	
	uint8_t i;
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);			//显示上半部分内容
	}
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);		//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)							
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)							
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	uint32_t i, j;
	
	for (i = 0; i < 1000; i++)			//上电延时
	{
		for (j = 0; j < 1000; j++);
	}
	
	OLED_I2C_Init();			//端口初始化
	
	OLED_WriteCommand(0xAE);	//关闭显示
	
	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);
	
	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);

	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);
	
	OLED_WriteCommand(0x40);	//设置显示开始行
	
	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置
	
	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);
	
	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/倒转显示

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示
		
	OLED_Clear();				//OLED清屏
}

unsigned char GRAM[1024]; 

/**
  * @brief  GRAM清屏
  * @param  无
  * @retval 无
  */
void OLED_GClear(void)
{  
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			GRAM[128*j+i]=0x00;
		}
	}
}


/**
  * @brief  GRAM右清屏
  * @param  无
  * @retval 无
  */
void OLED_GClear_R(void)
{  
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 63; i < 128; i++)
		{
			GRAM[128*j+i]=0x00;
		}
	}
}

/**
  * @brief  GRAM左清屏
  * @param  无
  * @retval 无
  */
void OLED_GClear_L(void)
{  
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 64; i++)
		{
			GRAM[128*j+i]=0x00;
		}
	}
}





//屏幕刷新
 void OLED_Refresh(void)
 {
     unsigned char x, y;
			int j=0;
     for(y=0; y<8; y++)
     {
         	OLED_SetCursor(y, 0);
         for(x=0; x<128; x++)
         {
             OLED_WriteData(GRAM[j++]);
         }
     }
 }

//画点
void OLED_DrawPoint(unsigned char x,unsigned char y)
{
	unsigned char n,m;
	n = y/8;
	m = y%8;
	GRAM[n*128+x] |= 0x01 << m;
}

/*
const uint8_t TU[][1024]= 
{
{
0xFF,0xFF,0xFF,0xFF,0x1F,0xCF,0x33,0x09,0x06,0x03,0x01,0x00,0x80,0x60,0xFC,0x58,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0x30,0x24,0x38,0x0D,0x02,0x09,0x04,
0x06,0x03,0x03,0x81,0xE0,0xFC,0xFF,0xFF,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0x7F,0x7F,0x3F,0x7F,0x3F,0xFF,0x7F,0x7F,0x7F,0x7F,0xDF,0xDF,0xFF,0xBE,
0xBF,0x3F,0x6F,0x6D,0x7E,0x4F,0xED,0xAB,0xFF,0xFB,0xF7,0x9F,0x53,0xEF,0x7F,0xBF,
0xDF,0xF7,0x7B,0xFF,0xFF,0xFE,0xFF,0xFB,0xFB,0xFF,0xFD,0xF3,0xE9,0x5E,0x07,0x80,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,},{
0xFF,0xFF,0x03,0xF8,0x07,0x00,0x00,0x00,0x00,0x00,0x68,0xE4,0x75,0x2F,0x1F,0x08,
0x2C,0x24,0x0A,0x10,0x00,0x00,0x00,0x00,0x80,0x30,0x04,0xF1,0x68,0x98,0x14,0x44,
0xC8,0x9C,0xF8,0x00,0x80,0x60,0x84,0xC1,0x10,0x06,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x40,0x38,0x3F,0xBF,0xBF,0xBB,0xFB,0xFB,0xFF,0xDF,0xFF,0xFD,0xF7,0x3D,0xBE,
0x1B,0x00,0x00,0x10,0x98,0xD4,0xF7,0xFC,0xF5,0xBF,0xFE,0xFF,0xFF,0xFF,0xFF,0xFE,
0xFF,0xFF,0xED,0xFE,0xDA,0xCE,0x82,0x06,0x8F,0x8F,0xAE,0xF9,0x90,0xE1,0xD7,0xFD,
0xEF,0xEF,0xFF,0xFF,0xEF,0xFF,0xFF,0x3F,0x07,0x1F,0x03,0x07,0x1F,0x07,0x0C,0x30,
0x60,0x60,0xE0,0xE0,0xC8,0xA8,0x30,0x00,0x20,0x00,0xC0,0x0E,0x00,0x00,0x00,0x00,},{
0xFF,0xFF,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x0E,0x0F,0x03,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x78,0x01,0x00,0x00,0x00,0x03,0x02,0x00,0x03,
0x01,0x01,0x00,0xD8,0x0F,0xC8,0x17,0x00,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x60,
0xAE,0xBF,0xFF,0xD7,0xFB,0xFE,0xFB,0xFF,0xF7,0xF7,0xF3,0xEB,0xFF,0xF7,0xDF,0xBB,
0xFF,0xD0,0xE1,0xE0,0xE9,0xFB,0xFB,0xEF,0xFB,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFD,0xF3,0xC0,0xFA,0xDF,0xF5,0xFF,
0xFF,0x3F,0x0F,0x0F,0x07,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x02,0x01,
0x04,0x17,0x13,0x10,0x11,0x11,0x10,0x03,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,},{
0xFF,0xFF,0xF8,0x87,0x7C,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0xAA,0x02,0xFE,0x06,0x00,0xF0,0x7F,0x7F,0x7F,0x3F,0x7F,0xC7,0xF8,0xFF,
0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0xFE,0xFE,0xFF,0xFF,0xFD,0xFF,0x7D,0xBE,0xFF,0xD6,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFB,0xD7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xF3,0x83,0x07,
0xFC,0xFC,0xFC,0xFC,0xFC,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,
0xF8,0xF8,0xF8,0x70,0x70,0x20,0x20,0xC0,0x80,0x00,0x80,0x80,0x00,0x00,0x00,0x00,},{
0xFF,0xFF,0xFF,0xFF,0xFC,0xE3,0x9E,0x70,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x7A,0x00,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0xC2,0xFC,0xBF,0xFF,0x7F,
0xFF,0xFD,0xFF,0xBF,0xFB,0xFF,0xBD,0xFD,0xDF,0xFF,0xAE,0xF7,0x79,0x1F,0x4F,0x67,
0x77,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xC7,0xEF,0xF7,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0xDF,0xF7,0xFD,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xF8,0x30,0x20,0x00,0x03,0x01,
0x01,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x03,0x07,0x02,0x01,0x03,0x03,0x02,0x00,},{
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xF9,0xF3,0xCC,0xB0,0x60,0x80,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x30,0x00,0x00,0x00,0x00,
0x00,0xD0,0xE5,0x6A,0x00,0x00,0x00,0x00,0x80,0xC0,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFE,0xFF,0xED,0xEF,0xDB,0xB7,0x6F,0xFF,0xBF,0x07,0x00,0x00,0x04,0x00,0x72,
0xF3,0xFF,0xFF,0xFD,0xAF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xBF,0xDF,0xCF,0xBB,0xFD,0xFF,0xFF,0xFF,0xFF,
0x5F,0x1F,0x1F,0x3F,0x1F,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x1F,0x7E,0xFC,
0xFC,0xF8,0xF8,0xF0,0x60,0x60,0x48,0x58,0x3C,0x3C,0x78,0x38,0x30,0x30,0x20,0x00,},{
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0xFC,0xFB,0xE6,
0xC8,0x90,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x08,0x40,
0x80,0x00,0x02,0x13,0x44,0x00,0xC0,0x00,0x3E,0xFF,0xFF,0xFF,0xFF,0xFF,0xFB,0xFF,
0xFF,0xFF,0xF7,0xFF,0xFF,0xFF,0xFF,0xEF,0xEF,0xED,0xE0,0xE0,0xC0,0x27,0xEF,0x6E,
0x6E,0x76,0x6E,0x7A,0x7F,0xBF,0xBF,0xF7,0xDF,0xDF,0xDF,0xBF,0x97,0xFF,0xEF,0xCD,
0xCE,0xC7,0xE7,0xE3,0xE1,0xE0,0x78,0x67,0x3F,0xB7,0xDF,0xFF,0xFB,0xFF,0xFB,0xFF,
0xFF,0xFC,0xF8,0xF8,0xF0,0xE0,0xC0,0x00,0x00,0x00,0x00,0x80,0x40,0xA0,0x00,0x00,
0x80,0x00,0x00,0x00,0x40,0x40,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,},{
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFC,0xF9,0xF2,0xCC,0x98,0x20,0x80,0x00,0x00,0x00,0x00,0x80,0x80,
0xC1,0xE2,0xF8,0xEC,0xFC,0xFC,0xFE,0xFC,0xFC,0xFC,0xFF,0xFF,0xD7,0xCF,0xF7,0x3F,
0xDF,0xFF,0xFF,0xFF,0xBF,0xFF,0xFF,0xFF,0x7F,0xBF,0xBF,0xBF,0xBF,0xBF,0x3F,0x3F,
0xBF,0xBF,0xBF,0xBF,0xFF,0xFF,0xDF,0xDF,0xDF,0xFF,0xEF,0xFF,0xFF,0xFF,0xF7,0x7F,
0xFB,0xAB,0xEB,0xE9,0xCF,0xEE,0xF7,0xE3,0xF0,0xB9,0xF7,0xD7,0xF6,0xFE,0xFE,0xFE,
0xFF,0xFF,0xBF,0xBD,0xFB,0xFF,0xD7,0xEE,0xF9,0xD8,0xE8,0x64,0x30,0x00,0x03,0x0A,
0x19,0x3B,0x1E,0x4C,0x3C,0x30,0x83,0x30,0x00,0x18,0x80,0x00,0x00,0x00,0x00,0x00,}





};
*/
//去点
void OLED_ClearPoint(unsigned char x,unsigned char y)
{
	unsigned char n,m;
	n = y/8;
	m = y%8;
	GRAM[n*128+x] &= ~(0x01<< m);
}

/*
 //四点阵
void FourPoints(u8 xstart, u8 ystart)
{
    u8 x,y;

    for(y=ystart; y<64; y+=2)
    {
        for(x=xstart; x<128; x+=2)
        {
            GRAM[y/8*128+x] |= 0x01 << (y%8);
        }
    }
}
//创建蒙版
void Mask_Gray(u8 xstart, u8 ystart)
{
    u8 x,y;
		u8 z; 
    for(y=ystart; y<64; y+=2)
    {
        for(x=xstart; x<128; x+=2)
        {
          z=TU[y/8][x]&(0x01<<(y%8));
					if(z)
					{
						OLED_DrawPoint(x,y);
					}
        }
    }
		OLED_Refresh();
}

//消除蒙版
void Mask_Gray_f(u8 xstart, u8 ystart)
{
    u8 x,y;
		u8 z; 
    for(y=ystart; y<64; y+=2)
    {
        for(x=xstart; x<128; x+=2)
        {
          z=TU[y/8][x]&(0x01<<(y%8));
					if(z)
					{
						OLED_ClearPoint(x,y);
					}
        }
    }
		OLED_Refresh();
}
	*/
//画线
void OLED_DrawLine(u8 x0, u8 y0, u8 x1, u8 y1, int draw)
{
    
		u8 lx = abs(x1-x0);
    u8 ly = abs(y1-y0);
    u8 dm, i;
    float dx, dy;
    float x, y;

    if(lx >= ly)
        dm = lx;
    else
        dm = ly;

    dx = (float)(x1-x0) / dm;
    dy = (float)(y1-y0) / dm;
    x = (float)x0 + 0.5;
    y = (float)y0 + 0.5;

    for(i=0; i<=dm; i++)
    {
        if(draw)
            OLED_DrawPoint(x, y);
        else
            OLED_ClearPoint(x,y);
        
        x += dx;
        y += dy;
    }
}
//波形
//unsigned char ADCdata[]={1,3,4,24,3,4,56,4,3,23,4,3,45,23,43,2,25,35,6,4,56,45,6,42,33,3,4,3,3,2,2,11,2,23,33,3};



/*
//显示波形图
void ADCWave( u8 step )
{
    u8 PointX, PointY;
    u8 Xtime;
    u8 range = 63;
    float scale = (float)range / 255;
    u8 PointXlast, PointYlast;

    PointX = 0;
    PointY = ADCdata[0] * scale;

    for(Xtime=0; Xtime<128; Xtime+=step)
    {
        PointXlast = PointX;
        PointYlast = PointY;

        PointX = Xtime;
        PointY = ADCdata[Xtime] * scale;

        // OLED_DrawPoint(PointX, 63-PointY);
        OLED_DrawLine(PointXlast, 63-PointYlast, PointX, 63-PointY, 1);
        OLED_Refresh(); Delay_ms(50);
    }
}
*/
//画sin
void OLED_DrawSin()
{
    u8 x0 = 64, y0 = 32;
    float Si, Sx, Sy, Rad;

    for(Si = -180; Si <180; Si += 1)
    {
        Rad = Si *3.14 /180;
        Sx = Si / 3.5;
        Sy = sin(Rad) * 25;

        OLED_DrawPoint(Sx + x0, 64-(Sy + y0));
        OLED_Refresh();
    }
}

//画圆1
void OLED_DrawCircle(u8 x0, u8 y0, u8 R, int draw)
{
    float Rx, Ry, angle, Rad;

    OLED_DrawPoint(x0, y0);

    for( angle = 0; angle <360; angle += 1 )
    {
        Rad = angle *3.14 / 180;
        Rx = R * cos(Rad);
        Ry = R * sin(Rad);

        if(draw == 1)
        {
            OLED_DrawPoint( x0 + Rx, 64 - (y0 + Ry) );
        }
        else if(draw == 0)
        {
            OLED_ClearPoint( x0 + Rx, 64 - (y0 + Ry) );
        }
        OLED_Refresh();
    }
}

//画圆2
void ShowCircle_Bresenham (u8 x0,u8 y0,int R)
{
    int x = 0;
    int y = R;
    int d = 3 - 2 * R;

    while (x <= y)
    {
        OLED_DrawPoint (x0 + x, y0 + y);
        OLED_DrawPoint (x0 - x, y0 + y);
        OLED_DrawPoint (x0 - x, y0 - y);
        OLED_DrawPoint (x0 + x, y0 - y);
        OLED_DrawPoint (x0 + y, y0 + x);
        OLED_DrawPoint (x0 - y, y0 + x);
        OLED_DrawPoint (x0 - y, y0 - x);
        OLED_DrawPoint (x0 + y, y0 - x);

        if(d < 0)
        {
            d += 4 * x + 6;
        }
        else
        {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
    OLED_Refresh();
}


// 功能：以(x0,y0)为圆心，绘制半径为2的小圆（复用已有画圆函数）
// 参数：
// - x0,y0：小圆的圆心坐标（与你原函数的参数含义一致）
//void OLED_ShowCircle(u8 x0, u8 y0, u8 R, int draw,int fill)
//{
//    // 直接调用你的ShowCircle_Bresenham，仅传圆心坐标和固定半径2
//    ShowCircle_Bresenham(x0, y0, 2);
//}

////旋转
//// 点的初始位置坐标 { x, y }
//float Vertex[2] = { 15, 5, };

//// 旋转函数
//void RotateXY ( float *x, float *y, float radian )
//{
//    float sinr = sin ( radian );
//    float cosr = cos ( radian );
//    float tempx = *x;
//    *x = tempx * cosr - *y * sinr;
//    *y = tempx * sinr + *y * cosr;
//}
//// 绘制旋转后的点
//void DrawRotated ( float radian )
//{
//    // 旋转点的坐标值，读取点的初始坐标值
//    float screenX = Vertex[ 0 ];
//    float screenY = Vertex[ 1 ];

//    // 旋转点，使用指针方式
//    RotateXY ( &screenX, &screenY, radian );

//    // 以旋转后的点的坐标为圆心，画小圆点
//		OLED_DrawPoint(screenX+OLED_WIDTH/2,63-( (int)screenY+OLED_HEIGHT/2 ));

//}





//几何体相关常量
#define PI 3.1415926
#define OLED_WIDTH 190
#define OLED_HEIGHT 64







//旋转
// 点的初始位置坐标 { x, y }
float Vertex1[][2] = { 
{14, 6 },
{18,2},
{6,-18},
	{18,-2},

};

//旋转
// 点的初始位置坐标 { x, y }
float Vertex2[][2] = { 
{0, 10 },
{3,0},
{0,-10},
	{-3,0},



};

//连接线端点
int GeometryEdges1[][2]={{0,1},{1,2},{2,3},{3,0}};

// 旋转函数
void RotateXY ( float *x, float *y, float radian )
{
    float sinr = sin ( radian );
    float cosr = cos ( radian );
    float tempx = *x;
    *x = tempx * cosr - *y * sinr;
    *y = tempx * sinr + *y * cosr;
}


// 绘制旋转后的点
void DrawRotated ( float radian )
{
    int i;
    for ( i = 0; i < CONNECTLINES1; i++ ) // 依次旋转每一个点
    {
        // 旋转点的坐标值，读取点的初始坐标值
        float startX = Vertex1[ GeometryEdges1[i][0] ][ 0 ]; //连线起点的x坐标
        float startY = Vertex1[ GeometryEdges1[i][0] ][ 1 ]; //连线起点的y坐标
        float endX = Vertex1[ GeometryEdges1[i][1] ][ 0 ]; //连线终点的x坐标
        float endY = Vertex1[ GeometryEdges1[i][1] ][ 1 ]; //连线终点的y坐标

        // 旋转点，使用指针方式
        RotateXY ( &startX, &startY, radian ); //旋转起点
        RotateXY ( &endX, &endY, radian ); //旋转终点

//        // 以旋转后的点的坐标为圆心，画小圆点
//        OLED_ShowCircle ( ( int ) startX + OLED_WIDTH / 2, 63 - ( ( int ) startY + OLED_HEIGHT / 2));

        // 画线
        OLED_DrawLine ( (int)startX+OLED_WIDTH/2, 63-((int)startY+OLED_HEIGHT/2), 
                        (int)endX+OLED_WIDTH/2, 63-((int)endY+OLED_HEIGHT/2), 1 );
    }
}

// 绘制旋转后的点
void DrawRotated1 ( float radian )
{
    int i;
    for ( i = 0; i < CONNECTLINES1; i++ ) // 依次旋转每一个点
    {
        // 旋转点的坐标值，读取点的初始坐标值
        float startX = Vertex2[ GeometryEdges1[i][0] ][ 0 ]; //连线起点的x坐标
        float startY = Vertex2[ GeometryEdges1[i][0] ][ 1 ]; //连线起点的y坐标
        float endX = Vertex2[ GeometryEdges1[i][1] ][ 0 ]; //连线终点的x坐标
        float endY = Vertex2[ GeometryEdges1[i][1] ][ 1 ]; //连线终点的y坐标

        // 旋转点，使用指针方式
        RotateXY ( &startX, &startY, radian ); //旋转起点
        RotateXY ( &endX, &endY, radian ); //旋转终点

//        // 以旋转后的点的坐标为圆心，画小圆点
//        OLED_ShowCircle ( ( int ) startX + OLED_WIDTH / 2, 63 - ( ( int ) startY + OLED_HEIGHT / 2));

        // 画线
        OLED_DrawLine ( (int)startX+OLED_WIDTH/2, 63-((int)startY+OLED_HEIGHT/2), 
                        (int)endX+OLED_WIDTH/2, 63-((int)endY+OLED_HEIGHT/2), 1 );
    }
}




void  DrawAxis(void)           // 画坐标轴
{
	OLED_DrawLine(0,32,127,32,1);
    OLED_DrawLine(64,0,64,63,1);
}


















#define SCALE_FACTOR 20  // 连线的缩放系数，用于将3D坐标投影到2D平面上
#define CONNECTLINES 12  // 连线数
#define FOCUS_DEPTH 9  // 焦点距
// 几何体顶点坐标数据 x y z
float Vertex[][3]={
    { 1,  1,  1 },  // 顶点0
    { 1,  1, -1 },  // 顶点1
    { -1, 1, -1 },  // 顶点2
    { -1, 1,  1 },  // 顶点3
    { 1, -1,  1 },  // 顶点4
    { 1, -1, -1 },  // 顶点5
    { -1, -1, -1 }, // 顶点6
    { -1, -1,  1 }, // 顶点7
};

// 连接的点（起点序号，终点序号）
int GeometryEdges[][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  // 上棱边
    {4, 5}, {5, 6}, {6, 7}, {7, 4},  // 下棱边
    {0, 4}, {1, 5}, {2, 6}, {3, 7},  // 竖棱边
};

// 旋转函数，绕轴旋转
void RotateZ(float *x, float *y, float radianZ) { // Z轴旋转
    float sinZ = sin(radianZ);
    float cosZ = cos(radianZ);
    float tempx = *x;
    *x = tempx * cosZ - *y * sinZ;
    *y = tempx * sinZ + *y * cosZ;
}

void RotateX(float *y, float *z, float radianX) { // X轴旋转
    float sinX = sin(radianX);
    float cosX = cos(radianX);
    float tempy = *y;
    *y = tempy * cosX - *z * sinX;
    *z = tempy * sinX + *z * cosX;
}

void RotateY(float *z, float *x, float radianY) { // Y轴旋转
    float sinY = sin(radianY);
    float cosY = cos(radianY);
    float tempz = *z;
    *z = tempz * cosY - *x * sinY;
    *x = tempz * sinY + *x * cosY;
}


// 投影到2D平面
void Project(float x, float y,float z, int *screenX, int *screenY)
{
    *screenX = (int)(x * SCALE_FACTOR*(FOCUS_DEPTH -z)/(FOCUS_DEPTH +1) + OLED_WIDTH / 2);
    *screenY = (int)(y * SCALE_FACTOR*(FOCUS_DEPTH -z)/(FOCUS_DEPTH +1) + OLED_HEIGHT / 2);
}






    int i;
    float startX3D, startY3D, startZ3D;  // 边的起点坐标
    float endX3D, endY3D, endZ3D;        // 边的终点坐标
    int startX, startY, endX, endY;      // 要画出的点的坐标



// 绘制旋转后的几何体
void DrawRotatedGeometry(float radianX, float radianY, float radianZ)
{


    for (i = 0; i < CONNECTLINES; i++)  // 遍历所有连线
    {
        // 获取当前边的起点、终点3D坐标
        startX3D = Vertex[GeometryEdges[i][0]][0];
        startY3D = Vertex[GeometryEdges[i][0]][1];
        startZ3D = Vertex[GeometryEdges[i][0]][2];
        endX3D = Vertex[GeometryEdges[i][1]][0];
        endY3D = Vertex[GeometryEdges[i][1]][1];
        endZ3D = Vertex[GeometryEdges[i][1]][2];

        // 旋转顶点
        RotateZ(&startX3D, &startY3D, radianZ);
        RotateZ(&endX3D, &endY3D, radianZ);
        RotateX(&startY3D, &startZ3D, radianX);
        RotateX(&endY3D, &endZ3D, radianX);
        RotateY(&startZ3D, &startX3D, radianY);
        RotateY(&endZ3D, &endX3D, radianY);

        // 投影到2D平面
        Project(startX3D, startY3D,startZ3D, &startX, &startY);
        Project(endX3D, endY3D,endZ3D, &endX, &endY);

        // 画线
        OLED_DrawLine(startX, 63-startY, endX, 63-endY, 1);
        
    }
}







void shiyan(float radianX,float radianY,float radianZ)
{
		//float angle;  // 角度值
		// 弧度值
	  DrawRotatedGeometry(radianX, radianY, radianZ);
    OLED_Refresh();  // 刷新OLED，显示新画面
}

void Biao(void)
{
	uint8_t a;
	float b;
	for(a=1;a>=0.5;a-=0.02)
	{
		b=a*a*a*sin(1/a);
		
	}
}












