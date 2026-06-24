/**
 * ================================================================
 * Fruit Ninja - 水果忍者（简化版）
 * ================================================================
 * 7种水果 + 炸弹，从屏幕底部抛起，抛物线飞行
 * 触摸滑动切水果，切开两半飞出，炸裂粒子
 * 三滴血，漏掉水果扣血，切炸弹直接结束
 * ================================================================
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "fruit_ninja.h"
#include <stdlib.h>
#include <math.h>

/* ==================== 屏幕尺寸 ==================== */
#define WIDTH    320
#define HEIGHT   480
#define HUD_H    44        /* 顶部信息栏高度 */

/* ==================== 颜色 (RGB565) ==================== */
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_ORANGE  0x05A1
#define C_PINK    0xF81F
#define C_PURPLE  0x480F
#define C_GRAY    0x18E3
#define C_DARKRED 0x7800
#define C_LTGREEN 0x03E0  /* 浅绿 */

/* ==================== 水果常量 ==================== */
#define MAX_FRUITS     8
#define FRUIT_R        18       /* 水果半径 */
#define GRAVITY        0.40f    /* 重力（正=向下）*/
#define SPAWN_Y        (HEIGHT - 60)  /* 从屏幕底部可见区 */
#define SPAWN_VX_MIN   (-4.0f)
#define SPAWN_VX_MAX   4.0f
#define SPAWN_VY_MIN   (-17.0f) /* 向上（负）*/
#define SPAWN_VY_MAX   (-13.0f)
#define SPAWN_MS       1500     /* 1.5秒出一个 */
#define BOMB_CHANCE    0.10     /* 10%炸弹 */

/* ==================== 粒子 ==================== */
#define MAX_PARTICLES  50

/* ==================== 水果类型 ==================== */
typedef enum {
    T_WATERMELON, T_ORANGE, T_APPLE, T_BANANA,
    T_STRAWBERRY, T_GRAPE, T_PINEAPPLE, T_COUNT
} FruitType;

typedef enum { S_WHOLE, S_SLICED, S_BOMB } FruitState;

/* ==================== 水果 ==================== */
typedef struct {
    uint8_t  on;          /* 激活 */
    uint8_t  sliced;      /* 已切 */
    FruitType type;
    FruitState state;
    float x, y;           /* 圆心 */
    float vx, vy;         /* 速度 */
    int   r;              /* 半径 */
    uint16_t color;       /* 主色 */
    uint16_t color2;      /* 切面色 */
    uint16_t color3;      /* 装饰色 */
} Fruit;
static Fruit fruits[MAX_FRUITS];

/* ==================== 切半 ==================== */
#define MAX_HALVES  (MAX_FRUITS * 2)
typedef struct {
    uint8_t on;
    float x, y;
    float vx, vy;
    uint16_t color;
    uint16_t color2;
    FruitType type;
    uint8_t left;   /* 1=左半, 0=右半 */
} FruitHalf;
static FruitHalf halves[MAX_HALVES];

/* ==================== 粒子 ==================== */
typedef struct {
    uint8_t on;
    float x, y;
    float vx, vy;
    uint16_t color;
    int life;
    int size;
} Particle;
static Particle parts[MAX_PARTICLES];

/* ==================== 滑动轨迹 ==================== */
#define TRAIL_MAX  20
typedef struct { float x, y; uint32_t t; } Trail;
static Trail trail[TRAIL_MAX];
static int trail_hd = 0, trail_cnt = 0;
static float last_x = -1, last_y = -1;

/* ==================== 游戏变量 ==================== */
static int score = 0;
static int lives = 3;
static uint8_t game_over = 0;
static uint8_t show_start = 1;
static uint32_t last_spawn = 0;

/* ================================================================
 * 工具函数
 * ================================================================ */

/* 伪随机 */
static int myrand(int lo, int hi){ return lo + rand()%(hi-lo+1); }

/* 画实心圆：逐行填充，逐行裁剪，保证水果出屏幕时不会画横条 */
static void fill_c(int cx, int cy, int r, uint16_t col)
{
    if(r <= 0) return;
    /* 圆完全在屏幕外则跳过 */
    if(cy + r < 0 || cy - r >= HEIGHT) return;
    if(cx + r < 0 || cx - r >= WIDTH)  return;
    int y_top = cy - r; if(y_top < 0)       y_top = 0;
    int y_bot = cy + r; if(y_bot >= HEIGHT) y_bot = HEIGHT - 1;
    int x_lim = cx - r; if(x_lim < 0)       x_lim = 0;
    int x_rim = cx + r; if(x_rim >= WIDTH)  x_rim = WIDTH  - 1;
    for(int yy = y_top; yy <= y_bot; yy++){
        int dy = yy - cy;
        int hw = (int)sqrtf((float)(r*r - dy*dy));
        if(hw < 1) hw = 1;
        /* 对每行单独裁剪左右边界，防止水果在屏幕边缘时画横线 */
        int lx = cx - hw; if(lx < x_lim) lx = x_lim;
        int rx = cx + hw; if(rx > x_rim) rx = x_rim;
        if(lx > rx) continue;
        lcd_fill(lx, yy, rx + 1, yy + 1, col);
    }
}

/* 画半圆：切面朝左或右，逐行裁剪边界 */
static void fill_half(int cx, int cy, int r, uint16_t col, uint16_t cut_col, uint8_t go_left)
{
    if(r <= 0) return;
    if(cy + r < 0 || cy - r >= HEIGHT) return;
    if(cx + r < 0 || cx - r >= WIDTH)  return;
    int y_top = cy - r; if(y_top < 0)        y_top = 0;
    int y_bot = cy + r; if(y_bot >= HEIGHT)  y_bot = HEIGHT - 1;
    int x_lim = cx - r; if(x_lim < 0)       x_lim = 0;
    int x_rim = cx + r; if(x_rim >= WIDTH)  x_rim = WIDTH - 1;
    for(int yy = y_top; yy <= y_bot; yy++){
        int dy = yy - cy;
        int hw = (int)sqrtf((float)(r*r - dy*dy));
        if(hw < 1) hw = 1;
        if(go_left){
            int lx = cx - hw; if(lx < x_lim) lx = x_lim;
            int rx = cx + 2;  if(rx  > x_rim) rx = x_rim;
            if(lx <= rx) lcd_fill(lx, yy, rx+1, yy+1, col);
            int lx2 = cx;     if(lx2 < x_lim) lx2 = x_lim;
            int rx2 = cx + 3; if(rx2  > x_rim) rx2 = x_rim;
            if(lx2 <= rx2) lcd_fill(lx2, yy, rx2+1, yy+1, cut_col);
        }else{
            int lx = cx - 2;  if(lx < x_lim) lx = x_lim;
            int rx = cx + hw; if(rx  > x_rim) rx = x_rim;
            if(lx <= rx) lcd_fill(lx, yy, rx+1, yy+1, col);
            int lx2 = cx - 3; if(lx2 < x_lim) lx2 = x_lim;
            int rx2 = cx + 1; if(rx2  > x_rim) rx2 = x_rim;
            if(lx2 <= rx2) lcd_fill(lx2, yy, rx2+1, yy+1, cut_col);
        }
    }
}

/* 画心形 */
static void draw_heart(int cx, int cy, int sz, uint16_t col)
{
    /* 用两个圆 + 三角组成 */
    int cr = sz/3; if(cr < 2) cr = 2;
    /* 左圆 */
    fill_c(cx - sz/3, cy, cr, col);
    /* 右圆 */
    fill_c(cx + sz/3, cy, cr, col);
    /* 下方三角 */
    for(int yy = cy; yy < cy + sz*2/3; yy++){
        int hw = (sz*2/3 - (yy - cy));
        if(hw < 1) hw = 1;
        lcd_fill(cx - hw, yy, cx + hw + 1, yy+1, col);
    }
}

/* ================================================================
 * 画水果
 * ================================================================ */

/* 画单个水果（全圆，简化版，每种只用2-3个圆）*/
static void draw_fruit(Fruit *f)
{
    if(!f->on) return;
    int x = (int)f->x, y = (int)f->y, r = f->r;

    switch(f->type){
        case T_WATERMELON:
            /* 西瓜：绿→浅绿→红瓤 */
            fill_c(x, y, r,   C_GREEN);
            fill_c(x, y, r-3, 0x03E0);
            fill_c(x, y, r-6, C_RED);
            fill_c(x-4, y-4, 2, C_BLACK); /* 籽 */
            fill_c(x+3, y+2, 2, C_BLACK);
            fill_c(x,   y+5, 2, C_BLACK);
            fill_c(x-5, y-6, 3, C_WHITE); /* 高光 */
            break;
        case T_ORANGE:
            fill_c(x, y, r,   C_ORANGE);
            fill_c(x, y, r-4, 0x03E0);
            fill_c(x-4, y-4, 4, C_WHITE); /* 高光 */
            break;
        case T_APPLE:
            fill_c(x, y, r,   C_RED);
            fill_c(x, y, r-4, C_DARKRED);
            fill_c(x-5, y-5, 4, C_WHITE); /* 高光 */
            /* 果柄 */
            lcd_fill(x-1, y-r-3, x+2, y-r+1, 0x0400);
            break;
        case T_BANANA:
            /* 香蕉：多个小圆排成弧形 */
            fill_c(x-8, y-3, 7, C_YELLOW); fill_c(x-8, y-3, 4, 0x07C0);
            fill_c(x-3, y-6, 7, C_YELLOW); fill_c(x-3, y-6, 4, 0x07C0);
            fill_c(x+3, y-7, 7, C_YELLOW); fill_c(x+3, y-7, 4, 0x07C0);
            fill_c(x+8, y-4, 7, C_YELLOW); fill_c(x+8, y-4, 4, 0x07C0);
            fill_c(x+2, y+1, 6, C_YELLOW); fill_c(x+2, y+1, 3, 0x07C0);
            break;
        case T_STRAWBERRY:
            /* 草莓：大红圆 */
            fill_c(x, y, r,   C_RED);
            fill_c(x, y, r-4, C_DARKRED);
            fill_c(x-4, y-4, 3, C_WHITE); /* 高光 */
            /* 籽 */
            fill_c(x,   y-3, 2, 0x03E0);
            fill_c(x-5, y+2, 2, 0x03E0);
            fill_c(x+5, y+2, 2, 0x03E0);
            /* 顶部叶子 */
            lcd_fill(x-6, y-r-2, x+6, y-r+1, C_GREEN);
            break;
        case T_GRAPE:
            /* 葡萄：一串小球 */
            fill_c(x-7, y+3, 7, C_PURPLE); fill_c(x-7, y+3, 3, 0x480F);
            fill_c(x,   y+3, 7, C_PURPLE); fill_c(x,   y+3, 3, 0x480F);
            fill_c(x+7, y+3, 7, C_PURPLE); fill_c(x+7, y+3, 3, 0x480F);
            fill_c(x-4, y-5, 7, C_PURPLE); fill_c(x-4, y-5, 3, 0x480F);
            fill_c(x+4, y-5, 7, C_PURPLE); fill_c(x+4, y-5, 3, 0x480F);
            fill_c(x,   y-12,6, C_PURPLE); fill_c(x,   y-12,2, 0x480F);
            break;
        case T_PINEAPPLE:
            /* 菠萝：椭圆 */
            fill_c(x, y, r,   0x05A1);
            fill_c(x, y, r-4, 0x0440);
            fill_c(x-4, y-4, 4, C_WHITE); /* 高光 */
            /* 叶子：几个小矩形 */
            lcd_fill(x-8, y-r-2, x+8, y-r+1, C_GREEN);
            lcd_fill(x-5, y-r-5, x+5, y-r-2, C_GREEN);
            lcd_fill(x-2, y-r-7, x+2, y-r-4, C_GREEN);
            break;
        default:
            fill_c(x, y, r, C_ORANGE);
            break;
    }
}

/* 画炸弹 */
static void draw_bomb(Fruit *f)
{
    if(!f->on) return;
    int x = (int)f->x, y = (int)f->y, r = f->r;
    fill_c(x, y, r,   C_BLACK);
    fill_c(x, y, r-5, 0x0820);
    fill_c(x-5, y-5, 4, C_WHITE); /* 高光 */
    /* 引线 */
    lcd_draw_line(x, y-r, x+3, y-r-6, C_GRAY);
    /* 火花 */
    if((HAL_GetTick()/200)%2==0)
        fill_c(x+3, y-r-6, 3, C_YELLOW);
    else
        fill_c(x+3, y-r-6, 2, C_RED);
}

/* ================================================================
 * 切半 & 粒子
 * ================================================================ */
static void do_slice(Fruit *f)
{
    if(f->state == S_BOMB){
        /* 炸！直接结束 */
        for(int i=0;i<25;i++){
            float a = i * 3.14159f * 2.0f / 25.0f;
            int px = (int)(f->x + cosf(a)*10);
            int py = (int)(f->y + sinf(a)*10);
            fill_c(px, py, myrand(3,6), C_YELLOW);
        }
        game_over = 1;
        f->on = 0;
        return;
    }

    /* 切水果：加分 */
    score += 10;

    /* 炸裂粒子 */
    int ci = 0;
    for(int i=0;i<MAX_PARTICLES && ci<15;i++){
        if(!parts[i].on){ parts[i].on=1;
            parts[i].x=f->x; parts[i].y=f->y;
            parts[i].vx=(float)(myrand(-6,6));
            parts[i].vy=(float)(myrand(-8,3));
            parts[i].color = f->color;
            parts[i].life  = 20+myrand(0,15);
            parts[i].size  = 2+myrand(0,4);
            ci++;
        }
    }
    for(int i=0;i<MAX_PARTICLES && ci<22;i++){
        if(!parts[i].on){ parts[i].on=1;
            parts[i].x=f->x; parts[i].y=f->y;
            parts[i].vx=(float)(myrand(-5,5));
            parts[i].vy=(float)(myrand(-7,2));
            parts[i].color = f->color2;
            parts[i].life  = 15+myrand(0,10);
            parts[i].size  = 2+myrand(0,3);
            ci++;
        }
    }

    /* 生成两半 */
    for(int i=0;i<MAX_HALVES;i+=2){
        if(!halves[i].on){
            halves[i].on=1; halves[i+1].on=1;
            halves[i].x=f->x; halves[i].y=f->y;
            halves[i].vx=f->vx-2.5f; halves[i].vy=f->vy-1.0f;
            halves[i].color=f->color; halves[i].color2=f->color2;
            halves[i].type=f->type; halves[i].left=1;

            halves[i+1].x=f->x; halves[i+1].y=f->y;
            halves[i+1].vx=f->vx+2.5f; halves[i+1].vy=f->vy-1.0f;
            halves[i+1].color=f->color; halves[i+1].color2=f->color2;
            halves[i+1].type=f->type; halves[i+1].left=0;
            break;
        }
    }

    f->on = 0;
}

/* ================================================================
 * 滑动碰撞检测
 * ================================================================ */
static int line_hits_circle(float x1,float y1,float x2,float y2,
                            float cx,float cy,float cr)
{
    float dx=x2-x1, dy=y2-y1;
    float fx=x1-cx, fy=y1-cy;
    float a=dx*dx+dy*dy;
    if(a < 0.001f) return 0;
    float b=2*(fx*dx+fy*dy);
    float c=fx*fx+fy*fy-cr*cr;
    float disc=b*b-4*a*c;
    if(disc<0) return 0;
    float sd=(float)sqrtf(disc);
    float t1=(-b-sd)/(2*a), t2=(-b+sd)/(2*a);
    if((t1>=0&&t1<=1)||(t2>=0&&t2<=1)||(t1<0&&t2>1)) return 1;
    return 0;
}

/* ================================================================
 * 生成水果
 * ================================================================ */
static void spawn_one(void)
{
    for(int i=0;i<MAX_FRUITS;i++){
        if(!fruits[i].on){
            Fruit *f = &fruits[i];
            f->on=1; f->sliced=0; f->state=S_WHOLE;
            f->x  = (float)myrand(40, WIDTH-40);
            f->y  = (float)SPAWN_Y;
            f->vx = (float)myrand((int)(SPAWN_VX_MIN*10),(int)(SPAWN_VX_MAX*10))/10.0f;
            f->vy = (float)myrand((int)(SPAWN_VY_MIN*10),(int)(SPAWN_VY_MAX*10))/10.0f;
            f->r  = FRUIT_R;
            if((rand()%100) < (int)(BOMB_CHANCE*100)){
                f->type=T_COUNT; f->state=S_BOMB;
                f->color=C_BLACK; f->color2=C_BLACK; f->color3=C_BLACK;
            }else{
                f->type=(FruitType)myrand(0,T_COUNT-1);
            }
            /* 颜色 */
            switch(f->type){
                case T_WATERMELON: f->color=C_GREEN;  f->color2=C_RED;    f->color3=0x03E0; break;
                case T_ORANGE:     f->color=C_ORANGE;  f->color2=0x03E0;   f->color3=C_WHITE; break;
                case T_APPLE:      f->color=C_RED;     f->color2=C_DARKRED;f->color3=C_WHITE; break;
                case T_BANANA:     f->color=C_YELLOW;  f->color2=0x07C0;  f->color3=C_WHITE; break;
                case T_STRAWBERRY: f->color=C_RED;     f->color2=C_DARKRED;f->color3=0x03E0; break;
                case T_GRAPE:      f->color=C_PURPLE;  f->color2=0x480F;  f->color3=C_WHITE; break;
                case T_PINEAPPLE:  f->color=0x05A1;   f->color2=0x0440;  f->color3=C_WHITE; break;
                default:           f->color=C_ORANGE;  f->color2=0x03E0;   f->color3=C_WHITE; break;
            }
            break;
        }
    }
}

/* ================================================================
 * 更新物理 + 屏幕碰撞（反弹）
 * ================================================================ */
static void update_all(void)
{
    /* 水果 */
    for(int i=0;i<MAX_FRUITS;i++){
        Fruit *f = &fruits[i];
        if(!f->on) continue;
        if(f->state==S_WHOLE || f->state==S_BOMB){
            f->x += f->vx;
            f->y += f->vy;
            f->vy += GRAVITY; /* 正数=向下 */

            int r = f->r;
            /* 左墙 */
            if(f->x - r < 0){
                f->x = (float)(r);
                f->vx = -f->vx * 0.75f;
            }
            /* 右墙 */
            if(f->x + r > WIDTH){
                f->x = (float)(WIDTH - r);
                f->vx = -f->vx * 0.75f;
            }
            /* 上边（跳过HUD区以下才检测） */
            if(f->y - r < HUD_H){
                f->y = (float)(HUD_H + r);
                f->vy = -f->vy * 0.75f;
            }
            /* 下边：水果漏掉扣血 */
            if(f->y - r > HEIGHT){
                if(f->state != S_BOMB){
                    lives--;
                    if(lives <= 0) game_over = 1;
                }
                f->on = 0;
            }
        }
    }
    /* 半边 */
    for(int i=0;i<MAX_HALVES;i++){
        if(!halves[i].on) continue;
        halves[i].x += halves[i].vx;
        halves[i].y += halves[i].vy;
        halves[i].vy += GRAVITY;
        /* 屏幕碰撞 */
        if(halves[i].x < 0){ halves[i].x = 0; halves[i].vx = -halves[i].vx*0.75f; }
        if(halves[i].x > WIDTH){ halves[i].x=(float)WIDTH; halves[i].vx=-halves[i].vx*0.75f; }
        if(halves[i].y < HUD_H){ halves[i].y=(float)(HUD_H+2); halves[i].vy=-halves[i].vy*0.75f; }
        if(halves[i].y > HEIGHT) halves[i].on = 0;
    }
    /* 粒子 */
    for(int i=0;i<MAX_PARTICLES;i++){
        if(!parts[i].on) continue;
        parts[i].x += parts[i].vx;
        parts[i].y += parts[i].vy;
        parts[i].vy += GRAVITY*0.5f;
        parts[i].life--;
        if(parts[i].life<=0) parts[i].on=0;
    }
}

/* ================================================================
 * 触摸处理
 * ================================================================ */
static void handle_touch(void)
{
    static uint8_t was_dn = 0;
    tp_scan(0);
    if(tp_dev.sta & TP_PRES_DOWN){
        uint16_t tx=tp_dev.x[0], ty=tp_dev.y[0];
        /* 记录轨迹 */
        trail[trail_hd%TRAIL_MAX].x=(float)tx;
        trail[trail_hd%TRAIL_MAX].y=(float)ty;
        trail[trail_hd%TRAIL_MAX].t=HAL_GetTick();
        trail_hd++; if(trail_cnt<TRAIL_MAX) trail_cnt++;

        if(was_dn && last_x>=0){
            float dx=(float)tx-last_x, dy=(float)ty-last_y;
            float dist=(float)sqrtf(dx*dx+dy*dy);
            if(dist>4.0f){ /* 有效滑动 */
                for(int i=0;i<MAX_FRUITS;i++){
                    Fruit *f=&fruits[i];
                    if(!f->on) continue;
                    /* 炸弹：切到直接爆炸结束 */
                    if(f->state==S_BOMB){
                        if(line_hits_circle(last_x,last_y,(float)tx,(float)ty,
                                             f->x,f->y,(float)f->r)){
                            f->on = 0;
                            do_slice(f); /* 炸出粒子，然后 game_over=1 */
                        }
                    }
                    /* 水果：切到生成半边+粒子+加分 */
                    else if(f->state==S_WHOLE){
                        if(line_hits_circle(last_x,last_y,(float)tx,(float)ty,
                                             f->x,f->y,(float)f->r)){
                            f->on = 0; /* 立即从渲染数组移除，避免下一帧画成default水果 */
                            do_slice(f);
                        }
                    }
                }
            }
        }
        was_dn=1;
        last_x=(float)tx; last_y=(float)ty;
    }else{
        was_dn=0; last_x=-1; last_y=-1;
    }
}

/* ================================================================
 * HUD：分数+血量
 * ================================================================ */
static void draw_hud(void)
{
    /* 上边黑条 */
    lcd_fill(0, 0, WIDTH, HUD_H, C_BLACK);
    /* 三颗心 */
    for(int i=0;i<3;i++){
        if(i<lives) draw_heart(20+i*28, 22, 20, C_RED);
        else        draw_heart(20+i*28, 22, 20, 0x1082);
    }
    /* 分数：直接在右上角显示数字 */
    lcd_show_num(220, 12, score, 5, 24, C_RED);
}

/* ================================================================
 * 滑动轨迹
 * ================================================================ */
static void draw_trail(void)
{
    uint32_t now=HAL_GetTick();
    for(int i=0;i<trail_cnt-1;i++){
        int i1=(trail_hd-trail_cnt+i)%TRAIL_MAX;
        int i2=(trail_hd-trail_cnt+i+1)%TRAIL_MAX;
        if(i1<0)i1+=TRAIL_MAX;
        if(i2<0)i2+=TRAIL_MAX;
        uint32_t age=now-trail[i1].t;
        if(age>200) continue;
        uint16_t col=(age<50)?C_WHITE:(age<120?0xC618:0x4208);
        lcd_draw_line((int)trail[i1].x,(int)trail[i1].y,
                      (int)trail[i2].x,(int)trail[i2].y,col);
    }
}

/* ================================================================
 * 开始画面
 * ================================================================ */
static void draw_start(void)
{
    lcd_fill(0,0,WIDTH,HEIGHT,C_BLACK);
    lcd_show_string(40,100,240,48,40,"Fruit",C_RED);
    lcd_show_string(40,155,240,48,40,"Ninja",C_YELLOW);
    /* 画几个水果做装饰 */
    Fruit tmp; int ox=80,oy=270;
    tmp.x=(float)ox;   tmp.y=(float)oy;   tmp.r=22; tmp.type=T_WATERMELON;
    tmp.color=C_GREEN; tmp.color2=C_RED; tmp.color3=0x03E0; draw_fruit(&tmp);
    tmp.x=(float)(ox+60); tmp.type=T_ORANGE;
    tmp.color=C_ORANGE; tmp.color2=0x03E0; draw_fruit(&tmp);
    tmp.x=(float)(ox+120); tmp.type=T_APPLE;
    tmp.color=C_RED; tmp.color2=C_DARKRED; draw_fruit(&tmp);
    tmp.x=(float)(ox+180); tmp.type=T_STRAWBERRY;
    tmp.color=C_RED; tmp.color2=C_DARKRED; tmp.color3=0x03E0; draw_fruit(&tmp);
    lcd_show_string(40,320,240,20,16,"Swipe to cut fruits!",C_WHITE);
    lcd_show_string(40,350,240,20,16,"Avoid the bombs!",C_YELLOW);
    lcd_show_string(60,385,200,20,16,"Tap to start",C_GRAY);
}

/* ================================================================
 * 结束画面
 * ================================================================ */
static void draw_over(void)
{
    lcd_fill(0,0,WIDTH,HEIGHT,C_BLACK);
    lcd_show_string(60,130,200,48,40,"GAME",C_RED);
    lcd_show_string(60,185,200,48,40,"OVER",C_RED);
    lcd_show_string(60,260,200,24,24,"Score:",C_WHITE);
    lcd_show_num(195, 260, score, 5, 24, C_YELLOW);
    lcd_show_string(60,305,200,20,16,"Tap to restart",C_GRAY);
    draw_heart(100,370,26,C_RED);
    draw_heart(140,370,26,C_RED);
    draw_heart(180,370,26,0x1082);
}

/* ================================================================
 * 初始化
 * ================================================================ */
static void init_game(void)
{
    for(int i=0;i<MAX_FRUITS;i++)     fruits[i].on = 0;
    for(int i=0;i<MAX_HALVES;i++)     halves[i].on = 0;
    for(int i=0;i<MAX_PARTICLES;i++) parts[i].on  = 0;
    trail_hd=0; trail_cnt=0; last_x=-1; last_y=-1;
    score=0; lives=3; game_over=0; show_start=1;
    last_spawn = HAL_GetTick();
}

/* ================================================================
 * 主循环
 * ================================================================ */
void fruit_ninja_game(void)
{
    init_game();
    draw_start();

    uint32_t last_draw = HAL_GetTick();

    while(1){
        uint32_t now = HAL_GetTick();

        /* -------- 开始画面 -------- */
        if(show_start){
            tp_scan(0);
            if(tp_dev.sta & TP_PRES_DOWN){
                delay_ms(300);
                tp_scan(0);
                if(!(tp_dev.sta & TP_PRES_DOWN)){
                    show_start = 0;
                    lcd_clear(C_BLACK);
                }
            }
            delay_ms(10);
            continue;
        }

        /* -------- 游戏结束 -------- */
        if(game_over){
            draw_over();
            tp_scan(0);
            if(!(tp_dev.sta & TP_PRES_DOWN)){
                delay_ms(300);
                tp_scan(0);
                if(!(tp_dev.sta & TP_PRES_DOWN)){
                    init_game();
                    draw_start();
                }
            }
            delay_ms(10);
            continue;
        }

        /* -------- 生成水果 -------- */
        if(now - last_spawn > SPAWN_MS){
            last_spawn = now;
            spawn_one();
        }

        /* -------- 更新 -------- */
        update_all();
        handle_touch();

        /* -------- 绘制（~30fps）-------- */
        if(now - last_draw >= 33){
            last_draw = now;

            /* 清屏：只清游戏区 */
            lcd_fill(0, 44, WIDTH, HEIGHT-44, C_BLACK);

            /* 炸弹 */
            for(int i=0;i<MAX_FRUITS;i++)
                if(fruits[i].on && fruits[i].state==S_BOMB) draw_bomb(&fruits[i]);

            /* 水果 */
            for(int i=0;i<MAX_FRUITS;i++)
                if(fruits[i].on && fruits[i].state==S_WHOLE) draw_fruit(&fruits[i]);

            /* 半边 */
            for(int i=0;i<MAX_HALVES;i++){
                if(!halves[i].on) continue;
                fill_half((int)halves[i].x, (int)halves[i].y, FRUIT_R,
                           halves[i].color, halves[i].color2, halves[i].left);
            }

            /* 粒子 */
            for(int i=0;i<MAX_PARTICLES;i++)
                if(parts[i].on)
                    fill_c((int)parts[i].x,(int)parts[i].y,parts[i].size,parts[i].color);

            /* 滑动轨迹 */
            draw_trail();

            /* HUD */
            draw_hud();
        }

        delay_ms(10);
    }
}
