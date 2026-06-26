#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "Timer.h"
#include <math.h>

#define PI 3.1415926
#define FORCE 0.3

static int h=22,m=30,s=10,ms=34;
static int h1=0;
static int h2=0;
static int h3=0;
static int e=0; //计时
	
void lingfang(void);
void GAME1(void);
void CLOCK(void);
void TIAO(void);
void jishi(void);
void MIAO(void);

int main(void)
{
	OLED_Init();
	Key_Init();
	Timer_Init();

	int mode=0,biao=1,change=0;
	
	while (1)
	{
		
		OLED_Showplayer(5,1,5);
		OLED_Showplayer(5,2,7);
		OLED_Showplayer(5,3,9);
		if(Key_Check(KEY_5, KEY_DOUBLE))
		{
			change=!change;
			OLED_GClear();
		}
		if(change)
		{
			mode=100;
			if (Key_Check(KEY_2, KEY_DOWN)&&biao<3 )
			{
				biao+=1;
			}	
			
			if (Key_Check(KEY_3, KEY_DOWN)&&biao>1 )
			{
				biao-=1;
			}	
			if(Key_Check(KEY_1, KEY_DOUBLE))
			{
				mode=biao;
				OLED_GClear ();
			}
			
			OLED_Showplayer(5,biao,2*biao+4);
		}
		if(!change)
		{
			mode=0;
			OLED_GClear ();
		}

		
		
		
		if(mode==0)
		{
			CLOCK();
			lingfang();
		}
		
		
		
		if(mode==1)
		{
			TIAO();
			h=h1;
			m=h2;
			s=h3;
			OLED_GClear();
		}
		
		
	
		
		if(mode==2)
		{
			GAME1();
			
		}

		if(mode==3)
		{
			MIAO();
			OLED_GClear();
		}
		
		
		
	}
}












void TIM2_IRQHandler(void)         //定时器
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		Key_Tick();
		ms++;
		jishi();

		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}


void GAME1(void)           //游戏函数
{
		uint16_t x=0x01;
		uint16_t y=0x01;
	while(1)
	{
		OLED_Showplayer(y, x, 2);
		if (Key_Check(KEY_1, KEY_HOLD) && y>1)
		{
			Delay_ms (100);
			OLED_Showplayer(y, x,1);
			y--;
		}
		if (Key_Check(KEY_4, KEY_HOLD) && y<8)
		{
			Delay_ms (100);
			OLED_Showplayer(y, x, 1);
			y++;
		}
		if (Key_Check(KEY_2, KEY_HOLD) && x<16)
		{
			Delay_ms (100);
			OLED_Showplayer(y, x,1);
			x++;
		}
		if (Key_Check(KEY_3, KEY_HOLD) && x>1)
		{
			Delay_ms (100);
			OLED_Showplayer(y, x, 1);
			x--;
		}
		if (Key_Check(KEY_5, KEY_DOUBLE))
		{
			break;
		}
	}
	OLED_GClear ();
}



void lingfang(void)        //灵方函数
{
		static float angle, radian;  // 角度值，弧度值
		static float a,b,i,c;
	
	  if(angle < 360)
    {
        angle+=10;
		OLED_GClear_R();        // 清屏
        radian = angle * PI / 180;  // 角度换算成弧度
        DrawRotated ( radian );  // 画旋转后的点
				DrawRotated ( radian+2 );  // 画旋转后的点
				DrawRotated ( radian+4 );  // 画旋转后的点
			
				DrawRotated1 ( -radian );  // 画旋转后的点
			
				DrawRotated1 ( -radian-1.5 );  // 画旋转后的点
    }
		else
		{
			angle=0;
		}

		//5号按键已启用，两个自带未成功
		
		if(!Key_Check(KEY_5, KEY_HOLD))
		{
			shiyan(a+=0.05,b+=0.05,c+=0.05);
		OLED_GClear_R();
		}

			
		if (Key_Check(KEY_1, KEY_DOWN) )
		{

		for(i=FORCE;i>=0.1;i/=1.5)
		{
			shiyan(a-=i,b,c);
			Delay_ms(20);
		OLED_GClear_R();
		}

		}
		if (Key_Check(KEY_4, KEY_DOWN) )
		{

		for(i=FORCE;i>=0.1;i/=1.5)
		{
			shiyan(a+=i,b,c);
			Delay_ms(20);
		OLED_GClear_R();
		}

		}
		if (Key_Check(KEY_2, KEY_DOWN) )
		{

		for(i=FORCE;i>=0.1;i/=1.5)
		{
			shiyan(a,b-=i,c);
			Delay_ms(20);
		OLED_GClear_R();
		}

		}
		if (Key_Check(KEY_3, KEY_DOWN) )
		{

		for(i=FORCE;i>=0.1;i/=1.5)
		{
			shiyan(a,b+=i,c);
			Delay_ms(20);
		OLED_GClear_R();
		}

		}


		
	//-----------------------------------

		
		if(Key_Check(KEY_1, KEY_HOLD) )
		{
			shiyan(a-=0.5*FORCE,b,c);
		OLED_GClear_R();
		}
		if (Key_Check(KEY_4, KEY_HOLD))
		{
			shiyan(a+=0.5*FORCE,b,c);
		OLED_GClear_R();
		}
		if (Key_Check(KEY_2, KEY_HOLD) )
		{
			shiyan(a,b-=0.5*FORCE,c);
		OLED_GClear_R();
		}
		if (Key_Check(KEY_3, KEY_HOLD))
		{
			shiyan(a,b+=0.5*FORCE,c);
		OLED_GClear_R();
		}
	}
		
		
	
	
void CLOCK(void)           //时钟函数
{
	OLED_ShowNum(1, 1,2025, 4);
	OLED_ShowNum(2, 1,h,2);
	if(s%2)
	{
	OLED_Showplayer(3,3,4);
	OLED_Showplayer(4,3,4);
	OLED_Showplayer(3,6,4);
	OLED_Showplayer(4,6,4);
	}
	OLED_ShowNum(2, 4,m,2);
	OLED_ShowNum(2, 7,s,2);

}


void jishi(void)           //防止不准
{
			if(ms>=1000)
			{
				ms=0;
				s++;
			}
			if(s>=60)
			{
				s=0;
				m++;
			}
			if(m>=60)
			{
				m=0;
				h++;
			}
			if(h>=24)
			{
				h=0;
			}
}



void TIAO(void)            //调时函数
{
	uint8_t q=2;
	while(1)
	{
		OLED_ShowNum(2, 1,h1,2);
		OLED_ShowNum(2, 4,h2,2);
		OLED_ShowNum(2, 7,h3,2);
		OLED_Showplayer(5, q, 5);
		if (Key_Check(KEY_2, KEY_DOWN) && q<8)
		{
			OLED_Showplayer(5, q,1);
			q+=3;
		}
		if (Key_Check(KEY_3, KEY_DOWN) && q>2)
		{
			OLED_Showplayer(5, q, 1);
			q-=3;
		}
		
		
		
		if (Key_Check(KEY_1, KEY_HOLD)   )
		{
			Delay_ms (100);
			if(q==2&& h1<23) h1++;
			if(q==5&& h2<59) h2++;
			if(q==8&& h3<59) h3++;
		}
		
		
		
		if (Key_Check(KEY_4, KEY_HOLD)   )
		{
						Delay_ms (100);
			if(q==2&& h1>0) h1--;
			if(q==5&& h2>0) h2--;
			if(q==8&& h3>0) h3--;
		}
		
		
		if (Key_Check(KEY_5, KEY_DOWN))
		{

			break;
		}
	}
}

void MIAO(void)
{
	uint8_t d=m;
	uint8_t f=s;
	uint8_t g=ms;
	int u,i,o,p;
	while(1)
	{
		jishi();
		int k2=60000*(m-d)+1000*(s-f)+(ms-g);
		u=k2/3600000;
		i=(k2-u*3600000)/60000;
		o=(k2-u*3600000-i*60000)/1000;
		p=k2-u*3600000-i*60000-o*1000;
		OLED_ShowNum(2, 1,i,2);
		if(s%2)
		{
			OLED_Showplayer(3,3,4);
			OLED_Showplayer(4,3,4);
			OLED_Showplayer(3,6,4);
			OLED_Showplayer(4,6,4);
		}
		OLED_ShowNum(2, 4,o,2);
		OLED_ShowNum(2, 7,p,3);
		
		
		
		if(Key_Check(KEY_5, KEY_DOUBLE))
		{
			break;
		}
	}
}

//

