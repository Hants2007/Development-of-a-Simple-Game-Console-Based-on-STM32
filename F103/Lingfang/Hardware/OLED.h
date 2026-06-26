#ifndef __OLED_H
#define __OLED_H
#include "stm32f10x.h"
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);


void OLED_Showplayer(uint8_t Line, uint8_t Column, char Char);



 void OLED_Refresh(void);       //刷新屏幕
void OLED_DrawPoint(unsigned char x,unsigned char y);//画点
void OLED_ClearPoint(unsigned char x,unsigned char y);//去点
void FourPoints(u8 xstart, u8 ystart);//四点阵
void Mask_Gray(u8 xstart, u8 ystart);//创建蒙版
void OLED_DrawLine(u8 x0, u8 y0, u8 x1, u8 y1, int draw);
void ADCWave( u8 step );
//void OLED_DrawSin();
void OLED_DrawCircle(u8 x0, u8 y0, u8 R, int draw);
void ShowCircle_Bresenham (u8 x0,u8 y0,int R);
void OLED_ShowCircle(u8 x0, u8 y0, u8 R, int draw,int fill);
void RotateXY ( float *x, float *y, float radian );
void DrawRotated ( float radian );
void DrawRotated1 ( float radian );
void  DrawAxis(void);
void shiyan(float radianX,float radianY,float radianZ);

void shiyan1(float radianX,float radianY,float radianZ);
void OLED_GClear(void);
void OLED_GClear_R(void);
void OLED_GClear_L(void);
void RotateZ(float *x, float *y, float radianZ);
void DrawRotatedGeometry(float radianX, float radianY, float radianZ);

#endif
