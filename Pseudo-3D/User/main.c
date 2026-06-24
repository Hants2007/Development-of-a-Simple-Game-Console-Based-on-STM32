/**
 * ================================================================
 * α3D FPS 游戏 - STM32F407 320x480 触摸屏版本(已改为摇杆控制)
 * ================================================================
 * 屏幕: 320x480 触摸屏
 * 控制: 游戏下方按钮区域已注释，改为摇杆控制
 * 
 * 【硬件连接说明】
 * 摇杆模块 -> STM32F407VGT6
 *   - VRX (X轴) -> PA0 (ADC1_IN0)
 *   - VRY (Y轴) -> PA1 (ADC1_IN1)
 *   - SW (按键)  -> PA2 (GPIO输入，低电平有效)
 *   - VCC        -> 3.3V (推荐) 或 5V (需确认摇杆规格)
 *   - GND        -> GND
 * ================================================================
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
/* 【已注释】触摸屏控制 - 改为摇杆控制 */
/* #include "./BSP/TOUCH/touch.h" */
#include "joystick.h"   /* 添加摇杆驱动头文件 */
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ==================== 硬件常量 (从main.c移植) ==================== */
#define LCD_WIDTH     320     /* 屏幕宽度 */
#define LCD_HEIGHT    480     /* 屏幕高度 */

/* 游戏区域 */
#define GAME_TOP      20
#define GAME_HEIGHT   240    /* 3D视口高度 */
#define GAME_LEFT     10
#define GAME_WIDTH    300     /* 游戏区域宽度 (320-10-10) */

/* 【已注释】屏幕按钮 - 底部80像素 */
/*
#define BTN_TOP       (GAME_TOP + GAME_HEIGHT + 10)
#define BTN_HEIGHT    80
#define BTN_ROWS      2
#define BTN_COLS      4
#define BTN_W         ((LCD_WIDTH - 20) / 4)   
#define BTN_H         35                         
#define BTN_SPACING   5
*/

/* 天花板/地板边界 */
#define FLOOR_Y       (GAME_TOP + GAME_HEIGHT / 2)

/* ==================== 地图参数 ==================== */
#define MAP_W         16
#define MAP_H         16
#define NUM_RAYS      100     /* 光线数量 */
#define MOVE_SPEED    15
#define ROT_SPEED     3      /* 旋转速度系数 */
#define MAX_DEPTH     20

/* ==================== 颜色 ==================== */
#define BG_COLOR      BLACK
#define CEIL_COLOR    0x1A1A3A   /* 深蓝天花板 */
#define FLOOR_COLOR   0x2A2A2A   /* 灰地板 */
/* 【已注释】按钮颜色 */
/*
#define BTN_COLOR     0x3186
#define BTN_PRESSED   0xF800
*/

/* ==================== 地图 ==================== */
static const uint8_t game_map[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1},
    {1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

/* ==================== 玩家 ==================== */
typedef struct {
    float x, y;
    float angle;
    float base_angle; /* 姿态模式基准角 */
    float pitch;      /* 俯仰角(0=看天, PI/2=水平, PI=看地) */
    float z;         /* 相机高度偏移(0=地面, 跳跃时>0) */
    float vz;        /* 垂直速度 */
} Player;

#define PLAYER_EYE_HEIGHT  0.5f   /* 眼睛离地高度(归一化,0=地面,1=天花板) */
#define JUMP_VELOCITY      0.15f /* 跳跃初速度 */
#define GRAVITY            0.03f /* 重力加速度(每帧) */
#define JUMP_GROUND_THRESH 0.2f /* 离地低于此值即可起跳 */
#define AIM_DEADZONE      0.99f  /* 摇杆2小于此值:姿态模式(直接映射角度) */
#define AIM_RANGE         0.08f /* 姿态模式最大偏转角度(弧度,约4.5度) */
#define PITCH_RANGE       120    /* 俯仰最大偏移像素 */

static Player player = {2.5f, 2.5f, 0.0f};

/* 【已注释】按钮结构体 (从main.c移植) */
/*
static struct {
    uint16_t x1, y1, x2, y2;
    uint8_t action;  
} buttons[4];

static struct {
    uint8_t left, right, down, up;
    uint8_t last_left, last_right, last_down, last_up;
} btn = {0};
*/

/* ==================== 摇杆控制变量 ==================== */
static Joystick_Data_t joy1 = {0};  /* 摇杆1: 移动 */
static Joystick_Data_t joy2 = {0};  /* 摇杆2: 视角 */
static uint8_t joystick_initialized = 0;

/* ==================== 渲染缓存 ==================== */
static int last_wall_top[NUM_RAYS];
static int last_wall_bottom[NUM_RAYS];
static uint16_t last_wall_color[NUM_RAYS];
static uint8_t first_frame = 1;

/* ==================== 地图检查 ==================== */
static uint8_t is_wall(int mx, int my) {
    if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return 1;
    return game_map[my][mx];
}

/* ==================== 光线投射 (DDA) ==================== */
static float cast_ray(float ray_angle, int *side) {
    float ray_dx = cosf(ray_angle);
    float ray_dy = sinf(ray_angle);
    
    int map_x = (int)player.x;
    int map_y = (int)player.y;
    
    float delta_dx = (ray_dx == 0) ? 1e30 : fabsf(1.0f / ray_dx);
    float delta_dy = (ray_dy == 0) ? 1e30 : fabsf(1.0f / ray_dy);
    
    int step_x, step_y;
    float side_dist_x, side_dist_y;
    
    if (ray_dx < 0) {
        step_x = -1;
        side_dist_x = (player.x - map_x) * delta_dx;
    } else {
        step_x = 1;
        side_dist_x = (map_x + 1.0f - player.x) * delta_dx;
    }
    
    if (ray_dy < 0) {
        step_y = -1;
        side_dist_y = (player.y - map_y) * delta_dy;
    } else {
        step_y = 1;
        side_dist_y = (map_y + 1.0f - player.y) * delta_dy;
    }
    
    int hit = 0;
    *side = 0;
    int depth = 0;
    
    while (!hit && depth < MAX_DEPTH) {
        if (side_dist_x < side_dist_y) {
            side_dist_x += delta_dx;
            map_x += step_x;
            *side = 0;
        } else {
            side_dist_y += delta_dy;
            map_y += step_y;
            *side = 1;
        }
        depth++;
        if (is_wall(map_x, map_y)) hit = 1;
    }
    
    float dist = (*side == 0) ? (side_dist_x - delta_dx) : (side_dist_y - delta_dy);
    return dist;
}

/* ==================== 颜色计算 ==================== */
static uint16_t get_wall_color(float dist, int side) {
    float shade = 1.0f - (dist / MAX_DEPTH);
    if (shade < 0) shade = 0;
    if (side == 1) shade *= 0.7f;
    
    uint8_t r = (uint8_t)(shade * 255);
    uint8_t g = (uint8_t)(shade * 200);
    uint8_t b = (uint8_t)(shade * 200);
    
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* ==================== 绘制天花板区域 ==================== */
static void draw_ceiling_region(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        int shade = ((h - j) * 40) / h;
        uint16_t color = ((shade >> 3) << 11) | ((shade >> 2) << 5) | (shade >> 3);
        lcd_draw_hline(x, y + j, w, color);
    }
}

/* ==================== 绘制地板区域 ==================== */
static void draw_floor_region(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        int shade = (j * 50) / h;
        uint16_t color = ((shade >> 3) << 11) | ((shade >> 2) << 5) | (shade >> 3);
        lcd_draw_hline(x, y + j, w, color);
    }
}

/* 【已注释】初始化按钮区域 */
/*
static void init_buttons(void) {
    buttons[0].x1 = 10;  buttons[0].y1 = BTN_TOP;
    buttons[0].x2 = 10 + BTN_W - 1;  buttons[0].y2 = BTN_TOP + BTN_H - 1;
    buttons[0].action = 0;
    
    buttons[1].x1 = 10 + BTN_W + 10;  buttons[1].y1 = BTN_TOP;
    buttons[1].x2 = buttons[1].x1 + BTN_W - 1;  buttons[1].y2 = BTN_TOP + BTN_H - 1;
    buttons[1].action = 1;
    
    buttons[2].x1 = 10;  buttons[2].y1 = BTN_TOP + BTN_H + 5;
    buttons[2].x2 = 10 + BTN_W - 1;  buttons[2].y2 = BTN_TOP + BTN_H + 5 + BTN_H - 1;
    buttons[2].action = 2;
    
    buttons[3].x1 = 10 + BTN_W + 10;  buttons[3].y1 = BTN_TOP + BTN_H + 5;
    buttons[3].x2 = buttons[3].x1 + BTN_W - 1;  buttons[3].y2 = BTN_TOP + BTN_H + 5 + BTN_H - 1;
    buttons[3].action = 3;
}
*/

/* 【已注释】绘制按钮 */
/*
static void draw_buttons(void) {
    const char *labels[] = {"左转", "右转", "后退", "前进"};
    
    for (int i = 0; i < 4; i++) {
        uint8_t pressed = 0;
        if (i == 0 && btn.left) pressed = 1;
        else if (i == 1 && btn.right) pressed = 1;
        else if (i == 2 && btn.down) pressed = 1;
        else if (i == 3 && btn.up) pressed = 1;
        
        lcd_fill(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2,
                 pressed ? BTN_PRESSED : BTN_COLOR);
        lcd_draw_rectangle(buttons[i].x1, buttons[i].y1, 
                          buttons[i].x2 + 1, buttons[i].y2 + 1, GRAY);
        
        int txt_x = buttons[i].x1 + 5;
        int txt_y = buttons[i].y1 + (BTN_H - 16) / 2;
        lcd_show_string(txt_x, txt_y, 60, 16, 16, (char*)labels[i], WHITE);
    }
}
*/

/* ==================== 显示状态信息 ==================== */
static void draw_info(void) {
    char info[30];
    snprintf(info, sizeof(info), "X:%.1f Y:%.1f A:%.0f", 
             player.x, player.y, player.angle * 180.0f / 3.1415926);
    lcd_show_string(10, GAME_TOP + GAME_HEIGHT + 2, 200, 12, 12, info, GREEN);
    
    /* 显示摇杆数据(调试用) */
    if (joystick_initialized) {
        char joy_info[40];
        snprintf(joy_info, sizeof(joy_info), "Joy: X%.2f Y%.2f SW%d", 
                 joy1.x_normalized, joy1.y_normalized, joy1.sw_pressed);
        lcd_show_string(10, GAME_TOP + GAME_HEIGHT + 16, 300, 12, 12, joy_info, YELLOW);
    }
}

/* ==================== 渲染帧 ==================== */
static void render_frame(void) {
    int strip_w = GAME_WIDTH / NUM_RAYS;
    
    float eye_h = PLAYER_EYE_HEIGHT + player.z;
    float fov = 1.2f;
    float ray_angle = player.angle - fov / 2.0f;
    float angle_step = fov / (float)(NUM_RAYS - 1);
    int pitch_shift = (int)(cosf(player.pitch) * PITCH_RANGE);
    float scale = GAME_HEIGHT / 2.0f;
    int screen_mid = GAME_TOP + GAME_HEIGHT / 2 + pitch_shift;
    
    for (int r = 0; r < NUM_RAYS; r++) {
        int side;
        float dist = cast_ray(ray_angle, &side);
        
        float fish_eye = ray_angle - player.angle;
        dist = dist * cosf(fish_eye);
        if (dist < 0.01f) dist = 0.01f;
        
        float wall_top_y = 1.0f - eye_h;
        float wall_bot_y = 0.0f - eye_h;
        float proj_top = wall_top_y / dist;
        float proj_bot = wall_bot_y / dist;
        
        int draw_top = screen_mid - (int)(proj_top * scale);
        int draw_bottom = screen_mid - (int)(proj_bot * scale);
        int x = GAME_LEFT + r * strip_w;
        
        if (draw_top < GAME_TOP) draw_top = GAME_TOP;
        if (draw_bottom > GAME_TOP + GAME_HEIGHT - 1) draw_bottom = GAME_TOP + GAME_HEIGHT - 1;
        
        uint16_t color = get_wall_color(dist, side);
        
        /* 天花板 */
        if (draw_top > GAME_TOP)
            lcd_fill(x, GAME_TOP, x + strip_w - 1, draw_top - 1, CEIL_COLOR);
        /* 墙壁 */
        if (draw_top <= draw_bottom)
            lcd_fill(x, draw_top, x + strip_w - 1, draw_bottom, color);
        /* 地板 */
        if (draw_bottom < GAME_TOP + GAME_HEIGHT - 1)
            lcd_fill(x, draw_bottom + 1, x + strip_w - 1, GAME_TOP + GAME_HEIGHT - 1, FLOOR_COLOR);
        
        ray_angle += angle_step;
    }
    
    draw_info();
}

/* 【已注释】触摸控制处理函数 */
/*
static void handle_touch(void) {
    tp_scan(0);
    
    btn.left = btn.right = btn.down = btn.up = 0;
    
    if (tp_dev.sta & TP_PRES_DOWN) {
        uint16_t tx = tp_dev.x[0];
        uint16_t ty = tp_dev.y[0];
        
        for (int i = 0; i < 4; i++) {
            if (tx >= buttons[i].x1 && tx <= buttons[i].x2 &&
                ty >= buttons[i].y1 && ty <= buttons[i].y2) {
                if (i == 0) btn.left = 1;
                else if (i == 1) btn.right = 1;
                else if (i == 2) btn.down = 1;
                else if (i == 3) btn.up = 1;
            }
        }
    }
    
    if (btn.left != btn.last_left || btn.right != btn.last_right ||
        btn.down != btn.last_down || btn.up != btn.last_up) {
        draw_buttons();
        btn.last_left = btn.left;
        btn.last_right = btn.right;
        btn.last_down = btn.down;
        btn.last_up = btn.up;
    }
}
*/

static void handle_joystick(void) {
    Joystick_Read(&joy1, &joy2);
}

/* ==================== 处理移动 ==================== */
static uint8_t handle_move(void) {
    uint8_t moved = 0;
    
    /* --- 跳跃(放在最前面,静止时也能触发) --- */
    static uint8_t sw_was_pressed = 0;
    if (joy1.sw_pressed && !sw_was_pressed && player.z <= JUMP_GROUND_THRESH) {
        player.vz = JUMP_VELOCITY;
    }
    sw_was_pressed = joy1.sw_pressed;
    
    /* 重力 */
    player.vz -= GRAVITY;
    player.z += player.vz;
    /* 落地检测 */
    if (player.z < 0.0f) {
        player.z = 0.0f;
        player.vz = 0.0f;
    }
    if (player.z > 0.0f || player.vz > 0.0f) moved = 1;
    
    /* --- 摇杆2: 视角控制 --- */
    float j2x = joy2.x_normalized;
    float j2abs = fabsf(j2x);
    if (j2abs > 0.05f) {
        if (j2abs <= AIM_DEADZONE) {
            /* 姿态模式: 摇杆位置直接映射到视角偏移 */
            player.angle = player.base_angle + j2x / AIM_DEADZONE * AIM_RANGE;
        } else {
            /* 速度模式: 超过阈值,恒速转动 */
            player.angle += 0.03f * ROT_SPEED * j2x;
            player.base_angle = player.angle;
        }
        moved = 1;
    } else {
        /* 摇杆回中,更新基准角 */
        player.base_angle = player.angle;
    }
    
    /* --- 摇杆2Y: 俯仰(直接映射0~PI, 中值滤波) --- */
    static float pitch_buf[5] = {1.5708f,1.5708f,1.5708f,1.5708f,1.5708f};
    static int pitch_idx = 0;
    float raw_pitch = (joy2.y_normalized + 1.0f) * 0.5f * 3.14159f;
    if (raw_pitch < 0.0f) raw_pitch = 0.0f;
    if (raw_pitch > 3.14159f) raw_pitch = 3.14159f;
    pitch_buf[pitch_idx % 5] = raw_pitch;
    pitch_idx++;
    /* 5点中值滤波 */
    float sorted[5];
    for (int i = 0; i < 5; i++) sorted[i] = pitch_buf[i];
    for (int i = 0; i < 4; i++) for (int j = i+1; j < 5; j++)
        if (sorted[i] > sorted[j]) { float t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
    player.pitch = sorted[2];
    moved = 1;  /* 俯仰变化也要刷新 */
    
    /* --- 摇杆1: 平移 --- */
    if (joystick_initialized) {
        float jx = joy1.x_normalized;
        float jy = joy1.y_normalized;
        
        float mag = sqrtf(jx * jx + jy * jy);
        if (mag < 0.05f) return moved;
        
        float speed = MOVE_SPEED * 0.01f * mag;
        float fwd_x = cosf(player.angle) * (-jy) * speed;
        float fwd_y = sinf(player.angle) * (-jy) * speed;
        float strafe_x = cosf(player.angle + 1.5708f) * jx * speed;
        float strafe_y = sinf(player.angle + 1.5708f) * jx * speed;
        
        float new_x = player.x + fwd_x + strafe_x;
        float new_y = player.y + fwd_y + strafe_y;
        
        if (!is_wall((int)new_x, (int)player.y)) player.x = new_x;
        if (!is_wall((int)player.x, (int)new_y)) player.y = new_y;
        moved = 1;
    }
    
    return moved;
}

/* ==================== 初始化 ==================== */
static void game_init(void) {
    HAL_Init();
    sys_stm32_clock_init(336, 8, 2, 7);
    delay_init(168);
    usart_init(115200);
    led_init();
    lcd_init();
    
    /* 【已注释】触摸屏初始化 */
    /* tp_init(); */
    
    /* 初始化摇杆 */
    Joystick_Init();
    joystick_initialized = 1;
    
    player.x = 2.5f;
    player.y = 2.5f;
    player.angle = 0.0f;
    player.base_angle = 0.0f;
    player.pitch = 1.5708f;  /* PI/2 = 水平 */
    
    /* 初始化墙壁缓存 */
    for (int r = 0; r < NUM_RAYS; r++) {
        last_wall_top[r] = GAME_TOP;
        last_wall_bottom[r] = GAME_TOP;
        last_wall_color[r] = BG_COLOR;
    }
    
    /* 【已注释】初始化按钮 */
    /* init_buttons(); */
    
    lcd_clear(BG_COLOR);
    lcd_show_string(100, 0, 120, 16, 16, "FPS 3D (Joystick)", WHITE);
    
    /* 【已注释】绘制按钮 */
    /* draw_buttons(); */
    
    /* 显示摇杆连接提示 */
    lcd_show_string(10, GAME_TOP + GAME_HEIGHT + 30, 300, 12, 12, 
                    "Joystick: PA0(VRX) PA1(VRY) PA2(SW)", CYAN);
    
    render_frame();
}

/* ==================== 游戏循环 ==================== */
static void game_loop(void) {
    /* 摇杆调试: 每次都刷新底部调试信息 */
    static uint16_t last_dbg_vrx = 0, last_dbg_vry = 0, last_dbg_vrx2 = 0;
    static uint8_t last_dbg_sw = 0xFF;
    
    while (1) {
        /* 读取摇杆 */
        handle_joystick();
        
        /* 调试: 始终显示摇杆原始ADC值 */
        if (joy1.vrx_value != last_dbg_vrx || 
            joy1.vry_value != last_dbg_vry ||
            joy2.vrx_value != last_dbg_vrx2 ||
            joy2.sw_pressed != last_dbg_sw) {
            lcd_fill(10, GAME_TOP + GAME_HEIGHT + 28, 310, GAME_TOP + GAME_HEIGHT + 40, BLACK);
            char dbg[60];
            snprintf(dbg, sizeof(dbg), "M:%d,%d V:%d,%d A:%.1f", 
                     joy1.vrx_value, joy1.vry_value,
                     joy2.vrx_value, joy2.vry_value,
                     player.angle * 57.3f);
            lcd_show_string(10, GAME_TOP + GAME_HEIGHT + 30, 300, 12, 12, dbg, RED);
            last_dbg_vrx = joy1.vrx_value;
            last_dbg_vry = joy1.vry_value;
            last_dbg_vrx2 = joy2.vrx_value;
            last_dbg_sw = joy2.sw_pressed;
        }
        
        if (handle_move()) {
            render_frame();
        }
        
        LED0_TOGGLE();
        delay_ms(30);
    }
}

/* ==================== 主函数 ==================== */
int main(void) {
    game_init();
    game_loop();
    return 0;
}
