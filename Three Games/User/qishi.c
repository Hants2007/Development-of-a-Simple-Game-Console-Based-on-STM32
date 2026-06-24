/**
 ****************************************************************************************************
 * @file        main.c
 * @author      MufengSong
 * @version     V9.0
 * @date        2026-04-07
 * @brief       元气骑士风格BSP随机地牢 + 触摸屏摇杆控制
 * @note        传送门通往下一层，格子放大到32px减少余光泄露
 ****************************************************************************************************
 */

/* ==================== 开发者模式 ==================== */
#define DEV_MODE       1    /* 1=开启（血量降为0不触发游戏结束）  0=关闭（正常游戏） */
#define DEBUG_MODE     1    /* 1=开启调试模式（所有武器放地面+出生点）  0=关闭（正常游戏） */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "qishi.h"
#include "dungeon_gen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ==================== 屏幕尺寸 ==================== */
#define LCD_WIDTH       320
#define LCD_HEIGHT      460      /* 游戏区域460px，屏幕实际480px */
#define Y_OFFSET        20       /* 游戏画面整体下移20px，顶部留给HUD */

/* ==================== 游戏参数 ==================== */
#define DUNGEON_COLS        40      /* 地牢列数 */
#define DUNGEON_ROWS        30      /* 地牢行数 */
#define CELL_SIZE           36      /* 瓦片大小（放大以减少余光看到其他房间）*/
#define PLAYER_SIZE         22      /* 玩家大小 */

#define JOYSTICK_RADIUS     50
#define JOYSTICK_KNOB_RADIUS 15
#define JOYSTICK_OFFSET_X   80
#define JOYSTICK_OFFSET_Y   80

#define PLAYER_SPEED        8.0f   /* 玩家移动速度(格子/秒) */

/* ==================== 地牢数据 ==================== */
static Dungeon g_dungeon;
static int g_playerRoom = 0;        /* 当前所在房间索引 */

/* ==================== 地刺控制 ==================== */
#define SPIKE_TOGGLE_MS       1500        /* 地刺弹出/收起周期(ms) */
#define SPIKE_DMG             1           /* 每次踩地刺扣1点 */
#define DEF_REGEN_MS          5000        /* 护盾恢复1格所需时间(ms) */

static uint32_t g_lastSpikeToggle = 0;
static uint8_t  g_spikesUp = 0;           /* 0=全部收起 1=全部弹出 */
static int8_t   g_lastSpikeHitX = -1;     /* 上次受伤的地刺格子X（-1=无）*/
static int8_t   g_lastSpikeHitY = -1;     /* 上次受伤的地刺格子Y */
static uint32_t g_lastDefRegen = 0;       /* 上次护盾恢复时间 */

/* 切换所有地刺状态（DOWN↔UP）*/
static void toggle_all_spikes(uint8_t up) {
    for (int r = 0; r < g_dungeon.height; r++) {
        for (int c = 0; c < g_dungeon.width; c++) {
            if (up) {
                if (g_dungeon.map[r][c] == TILE_SPIKE_DOWN)
                    g_dungeon.map[r][c] = TILE_SPIKE_UP;
            } else {
                if (g_dungeon.map[r][c] == TILE_SPIKE_UP)
                    g_dungeon.map[r][c] = TILE_SPIKE_DOWN;
            }
        }
    }
}

/* ==================== 门控制 ==================== */
static uint32_t g_lastDoorToggle = 0;   /* 上次门切换时间 */
static uint8_t  g_doorsOpen = 0;        /* 0=全部关闭 1=全部打开 */
#define DOOR_TOGGLE_MS   1000            /* 每隔1秒切换一次 */

/* 切换所有门的状态（就地修改g_dungeon.map） */
static void toggle_all_doors(uint8_t open) {
    for (int r = 0; r < g_dungeon.height; r++) {
        for (int c = 0; c < g_dungeon.width; c++) {
            if (open) {
                if (g_dungeon.map[r][c] == TILE_DOOR_CLOSED)
                    g_dungeon.map[r][c] = TILE_DOOR_OPEN;
            } else {
                if (g_dungeon.map[r][c] == TILE_DOOR_OPEN)
                    g_dungeon.map[r][c] = TILE_DOOR_CLOSED;
            }
        }
    }
}

/* ==================== 玩家状态 ==================== */
static float playerX = 0.0f;
static float playerY = 0.0f;
static uint8_t gun = 0;

/* ==================== 商品系统（全局变量必须在使用前声明）==================== */
#define ITEM_POTION         0   /* 生命药水 */
#define ITEM_WEAPON         1   /* 武器 */
#define ITEM_ENERGY_POTION  2   /* 能量药水 */
static int g_itemPos[5][2] = {{-1,-1}, {-1,-1}, {-1,-1}, {-1,-1}, {-1,-1}};  /* 五个商品的位置 */
static uint8_t g_itemType[5] = {0, 0, 0, 0, 0};                   /* 商品类型 */
static int g_itemCost[5] = {0, 0, 0, 0, 0};                       /* 商品价格（生成时确定）*/

/* ==================== 商店/彩蛋机辅助函数 ==================== */
/* 检查指定格子是否已被商品占用 */
static int is_item_pos_taken(int ix, int iy) {
    for (int i = 0; i < 5; i++) {
        if (g_itemPos[i][0] == ix && g_itemPos[i][1] == iy) return 1;
    }
    return 0;
}

/* 查找商店周围最近的空地板位置（距离商店1~maxDist格）*/
static int find_nearest_floor_around(int baseX, int baseY, int *outX, int *outY, int maxDist) {
    for (int dist = 1; dist <= maxDist; dist++) {
        for (int dy = -dist; dy <= dist; dy++) {
            for (int dx = -dist; dx <= dist; dx++) {
                if (abs(dx) != dist && abs(dy) != dist) continue;
                int ix = baseX + dx;
                int iy = baseY + dy;
                if (ix < 0 || ix >= DUNGEON_COLS || iy < 0 || iy >= DUNGEON_ROWS) continue;
                if (g_dungeon.map[iy][ix] == TILE_FLOOR && !is_item_pos_taken(ix, iy)) {
                    *outX = ix;
                    *outY = iy;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ==================== 彩蛋机系统 ==================== */
#define GACHAPON_BASE_COST    20   /* 彩蛋机基础花费 */
#define GACHAPON_LEVEL_BONUS  10   /* 每关增加的花费 */
#define GACHAPON_RANDOM_RANGE 5    /* 随机波动范围 ±5 */

static int g_gachaponX = -1;        /* 彩蛋机位置 */
static int g_gachaponY = -1;
static uint32_t g_gachaponAnimStart = 0;  /* 动画开始时间 */
static int g_gachaponAnimActive = 0;      /* 动画是否激活 */
static uint8_t g_gachaponResult = 0;      /* 吐出的武器类型 */
static uint8_t g_gachaponUsed = 0;        /* 本关是否已使用（0=未使用，1=已使用）*/

/* ==================== 商店系统 ==================== */
#define SHOP_POTION_BASE_COST    20   /* 生命药水基础花费 */
#define SHOP_WEAPON_BASE_COST    40   /* 武器基础花费（根据威力调整）*/
#define SHOP_LEVEL_BONUS         10   /* 每关增加的花费 */
#define SHOP_RANDOM_RANGE        5    /* 随机波动范围 ±5 */

/* 商店位置（占2格）*/
static int g_shopX = -1;        /* 商店左格X */
static int g_shopY = -1;        /* 商店左格Y */

/* ==================== 武器定义（提前定义供价格计算使用）==================== */
#define WEAPON_PISTOL    0       /* 手枪：单发 */
#define WEAPON_SHOTGUN   1       /* 霰弹枪：三发散射 */
#define WEAPON_SMG       2       /* 冲锋枪：连发，0.3秒/发 */
#define WEAPON_LASER     3       /* 激光炮：长按0.3秒充能，持续光束 */
#define WEAPON_STAFF     4       /* 法杖：四面八方一圈大慢弹 */
#define WEAPON_ROCKET    5       /* 火箭炮：发射火箭弹，命中爆炸(3×3) */

/* ==================== 摇杆状态 ==================== */
static int joystickActive = 0;
static float joystickDirX = 0.0f;
static float joystickDirY = 0.0f;
static float lastjoystickDirX = 0.0f;
static float lastjoystickDirY = 0.0f;
static uint16_t knobX;
static uint16_t knobY;
static uint16_t joystickCenterX;
static uint16_t joystickCenterY;

/* ==================== 像素地图（32位位图）==================== */
#define WORDS_PER_LINE  ((LCD_WIDTH + 31) / 32)
static uint32_t pixelMap[LCD_HEIGHT][WORDS_PER_LINE];

/* 清除像素地图 */
static void clearPixelMap(void) {
    memset(pixelMap, 0, sizeof(pixelMap));
}

/* ==================== 带像素地图的绘图函数 ==================== */
static void draw_point_with_map(int x, int y, uint16_t color) {
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    lcd_draw_point(x, y + Y_OFFSET, color);
    pixelMap[y][x >> 5] |= (1UL << (x & 31));
}

static void fill_rect_with_map(int x1, int y1, int x2, int y2, uint16_t color) {
    if (x1 > x2) { int tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { int tmp = y1; y1 = y2; y2 = tmp; }
    if (x2 < 0 || x1 >= LCD_WIDTH || y2 < 0 || y1 >= LCD_HEIGHT) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= LCD_WIDTH) x2 = LCD_WIDTH - 1;
    if (y2 >= LCD_HEIGHT) y2 = LCD_HEIGHT - 1;

    lcd_fill(x1, y1 + Y_OFFSET, x2, y2 + Y_OFFSET, color);
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            pixelMap[y][x >> 5] |= (1UL << (x & 31));
        }
    }
}

static void draw_line_with_map(int x1, int y1, int x2, int y2, uint16_t color) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x2 > x1) ? 1 : -1;
    int sy = (y2 > y1) ? 1 : -1;
    int err = dx - dy;
    int x = x1, y = y1;
    while (1) {
        draw_point_with_map(x, y, color);
        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

static void draw_rect_with_map(int x1, int y1, int x2, int y2, uint16_t color) {
    draw_line_with_map(x1, y1, x2, y1, color);
    draw_line_with_map(x1, y2, x2, y2, color);
    draw_line_with_map(x1, y1, x1, y2, color);
    draw_line_with_map(x2, y1, x2, y2, color);
}

static void draw_circle_with_map(int x0, int y0, int r, uint16_t color) {
    int a = 0, b = r;
    int d = 1 - r;
    while (a <= b) {
        draw_point_with_map(x0 + a, y0 + b, color);
        draw_point_with_map(x0 - a, y0 + b, color);
        draw_point_with_map(x0 + a, y0 - b, color);
        draw_point_with_map(x0 - a, y0 - b, color);
        draw_point_with_map(x0 + b, y0 + a, color);
        draw_point_with_map(x0 - b, y0 + a, color);
        draw_point_with_map(x0 + b, y0 - a, color);
        draw_point_with_map(x0 - b, y0 - a, color);
        a++;
        if (d < 0) d += 2 * a + 1;
        else { b--; d += 2 * (a - b) + 1; }
    }
}

/* 快速填充圆形 */
static void fill_circle_fast(int x0, int y0, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        int lineWidth = (int)sqrtf(r * r - y * y);
        int x1 = x0 - lineWidth;
        int x2 = x0 + lineWidth;
        if (y0 + y < 0 || y0 + y >= LCD_HEIGHT) continue;
        if (x1 < 0) x1 = 0;
        if (x2 >= LCD_WIDTH) x2 = LCD_WIDTH - 1;
        if (x1 <= x2) {
            fill_rect_with_map(x1, y0 + y, x2, y0 + y, color);
        }
    }
}

/* ==================== 背景清除：只重绘像素地图未标记的区域 ==================== */
static void fill_background_from_map(void) {
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int w = 0; w < WORDS_PER_LINE; w++) {
            uint32_t word = pixelMap[y][w];
            if (word == 0) {
                int x_start = w * 32;
                int x_end = x_start + 31;
                if (x_end >= LCD_WIDTH) x_end = LCD_WIDTH - 1;
                lcd_fill(x_start, y + Y_OFFSET, x_end, y + Y_OFFSET, BLACK);
            } else if (word != 0xFFFFFFFF) {
                for (int bit = 0; bit < 32; bit++) {
                    int x = w * 32 + bit;
                    if (x >= LCD_WIDTH) break;
                    if (!(word & (1UL << bit))) {
                        lcd_draw_point(x, y + Y_OFFSET, BLACK);
                    }
                }
            }
        }
    }
}

/* ==================== 传送门变量 ==================== */
static int g_portalRoom = -1;       /* 传送门所在房间 */
static int g_portalX = 0;           /* 传送门格子坐标 */
static int g_portalY = 0;
static int g_dungeonLevel = 1;      /* 当前层数 */
static int g_bossDefeated = 0;       /* BOSS是否被击败（BOSS关卡需要击败BOSS才能进传送门）*/
static uint8_t g_portalFlash = 0;   /* 传送门闪烁计数器 */
static uint8_t g_levelTransition = 0;/* 层切换动画中 */

/* 计算商店商品价格（生成时调用，价格固定）*/
static int calc_shop_item_cost(int isPotion, int weaponType) {
    int base;
    if (isPotion) {
        base = SHOP_POTION_BASE_COST + (g_dungeonLevel - 1) * SHOP_LEVEL_BONUS;
    } else {
        /* 武器根据威力定价 */
        switch (weaponType) {
            case WEAPON_PISTOL:   base = 25; break;  /* 最弱 */
            case WEAPON_SHOTGUN:  base = 40; break;
            case WEAPON_LASER:    base = 55; break;
            case WEAPON_STAFF:    base = 70; break;
            case WEAPON_ROCKET:   base = 95; break;  /* 最强 */
            case WEAPON_SMG:      base = 85; break;  /* 最强 */
            default:              base = 40; break;
        }
        base += (g_dungeonLevel - 1) * SHOP_LEVEL_BONUS;
    }
    int randomAdd = dungeon_rand(&g_dungeon, SHOP_RANDOM_RANGE * 2 + 1) - SHOP_RANDOM_RANGE;
    return base + randomAdd;
}

/* 计算当前关卡彩蛋机花费 */
static int get_gachapon_cost(void) {
    int base = GACHAPON_BASE_COST + (g_dungeonLevel - 1) * GACHAPON_LEVEL_BONUS;
    int randomOffset = dungeon_rand(&g_dungeon, GACHAPON_RANDOM_RANGE * 2 + 1) - GACHAPON_RANDOM_RANGE;
    return base + randomOffset;
}

/* 随机抽取武器（概率：冲锋枪15%，法杖20%，霰弹枪25%，激光炮20%，手枪20%）*/
static uint8_t gachapon_roll_weapon(void) {
    int roll = dungeon_rand(&g_dungeon, 100);
    if (roll < 8) {
        return TILE_WEAPON_ROCKET;     /* 8% 火箭炮（最稀有）*/
    } else if (roll < 23) {
        return TILE_WEAPON_SMG;        /* 15% 冲锋枪 */
    } else if (roll < 43) {
        return TILE_WEAPON_STAFF;      /* 20% 法杖 */
    } else if (roll < 63) {
        return TILE_WEAPON_LASER;      /* 20% 激光炮 */
    } else if (roll < 88) {
        return TILE_WEAPON_SHOTGUN;    /* 25% 霰弹枪 */
    } else {
        return TILE_WEAPON_PISTOL;     /* 12% 手枪 */
    }
}

/* ==================== 传送动画 ==================== */
/* 阶段1: 白光闪烁 (500ms)
 * 阶段2: 彩色同心圆扩散 (800ms) 
 * 阶段3: 黑屏 (300ms)
 * 阶段4: 淡入新层 (500ms)
 * 总计约2秒 */
static void run_transition_animation(void) {
    int i, j;
    int cx = LCD_WIDTH / 2;
    int cy = LCD_HEIGHT / 2 + Y_OFFSET;
    uint16_t ring_colors[] = {DARKBLUE, BLUE, MAGENTA, 0x8010, RED, DARKBLUE};
    
    /* 阶段1: 漩涡扩散 — 彩色同心圆从中心向外涌出 (8帧) */
    for (i = 0; i < 8; i++) {
        lcd_clear(BLACK);
        for (j = 0; j < 6; j++) {
            int r = i * 15 - j * 20;
            if (r > 5 && r < 240) {
                lcd_draw_circle(cx, cy, (r > 0) ? r : 5, ring_colors[j]);
            }
        }
        delay_ms(100);
    }
    
    /* 阶段2: 漩涡收缩 — 圆环向中心收缩，模拟被吸入 (8帧) */
    for (i = 7; i >= 0; i--) {
        lcd_clear(BLACK);
        for (j = 0; j < 6; j++) {
            int r = i * 15 - j * 20;
            if (r > 5 && r < 240) {
                /* 收缩时颜色反转，营造旋转感 */
                lcd_draw_circle(cx, cy, (r > 0) ? r : 5, ring_colors[5 - j]);
            }
        }
        delay_ms(80);
    }
    
    /* 阶段3: 漩涡消失 — 中心亮点缩成一个小点 */
    for (i = 60; i > 0; i -= 5) {
        lcd_clear(BLACK);
        lcd_draw_circle(cx, cy, i, MAGENTA);
        delay_ms(40);
    }
    lcd_draw_point(cx, cy, WHITE);
    delay_ms(200);
    
    /* 阶段4: 黑屏 + 显示层数 */
    lcd_clear(BLACK);
    {
        char buf[20];
        int lv = g_dungeonLevel;
        buf[0] = 'L'; buf[1] = 'v'; buf[2] = '.';
        if (lv >= 100) { buf[3] = '0' + lv/100; lv %= 100; } else { buf[3] = ' '; }
        if (lv >= 10)  { buf[4] = '0' + lv/10;  lv %= 10;  } else { buf[4] = ' '; }
        buf[5] = '0' + lv;
        buf[6] = 0;
        lcd_show_string(cx - 40, cy - 8, 400, 32, 32, buf, MAGENTA);
    }
    delay_ms(800);
}

/* ==================== 绘制地牢（使用BSP生成器）==================== */
static void render_dungeon(void) {
    int screenCenterX = LCD_WIDTH / 2;
    int screenCenterY = LCD_HEIGHT / 2 - 40;  /* 稍微上移，给摇杆留空间 */

    float worldPlayerCenterX = (playerX + 0.5f) * CELL_SIZE;
    float worldPlayerCenterY = (playerY + 0.5f) * CELL_SIZE;
    float offsetX = worldPlayerCenterX - screenCenterX;
    float offsetY = worldPlayerCenterY - screenCenterY;

    for (int row = 0; row < DUNGEON_ROWS; row++) {
        for (int col = 0; col < DUNGEON_COLS; col++) {
            int worldLeft = col * CELL_SIZE;
            int worldTop = row * CELL_SIZE;
            int screenLeft = (int)(worldLeft - offsetX);
            int screenTop = (int)(worldTop - offsetY);
            
            if (screenLeft + CELL_SIZE > 0 && screenLeft < LCD_WIDTH &&
                screenTop + CELL_SIZE > 0 && screenTop < LCD_HEIGHT) {
                
                uint8_t tile = g_dungeon.map[row][col];
                
                if (tile == TILE_WALL) {
                    /* 墙壁 - 深绿色 */
                    fill_rect_with_map(screenLeft, screenTop, 
                                       screenLeft + CELL_SIZE - 1, 
                                       screenTop + CELL_SIZE - 1, 
                                       GREEN);
                    /* 简单的格子装饰 */
                    draw_rect_with_map(screenLeft + 2, screenTop + 2,
                                       screenLeft + CELL_SIZE - 3,
                                       screenTop + CELL_SIZE - 3, BLACK);
                } else if (tile == TILE_FLOOR) {
                    /* 地板 - 浅灰色 */
                    fill_rect_with_map(screenLeft, screenTop,
                                       screenLeft + CELL_SIZE - 1,
                                       screenTop + CELL_SIZE - 1,
                                       GRAY);
                } else if (tile == TILE_DOOR_CLOSED) {
                    /* 关闭的门 - 棕色 + 竖线装饰 */
                    fill_rect_with_map(screenLeft, screenTop,
                                       screenLeft + CELL_SIZE - 1,
                                       screenTop + CELL_SIZE - 1,
                                       BROWN);
                    /* 画两条竖线表示门板 */
                    int mid = screenLeft + CELL_SIZE / 2;
                    draw_line_with_map(mid - 3, screenTop + 2,
                                       mid - 3, screenTop + CELL_SIZE - 3, BLACK);
                    draw_line_with_map(mid + 3, screenTop + 2,
                                       mid + 3, screenTop + CELL_SIZE - 3, BLACK);
                    /* 门的把手 */
                    draw_point_with_map(mid, screenTop + CELL_SIZE / 2, YELLOW);
                } else if (tile == TILE_DOOR_OPEN) {
                    /* 打开的门 - 地板色 + 淡边框 */
                    fill_rect_with_map(screenLeft, screenTop,
                                       screenLeft + CELL_SIZE - 1,
                                       screenTop + CELL_SIZE - 1,
                                       GRAY);
                    /* 画淡色边框表示门框 */
                    draw_rect_with_map(screenLeft + 1, screenTop + 1,
                                       screenLeft + CELL_SIZE - 2,
                                       screenTop + CELL_SIZE - 2, CYAN);
                } else if (tile == TILE_PORTAL) {
                    /* 传送门 - 蓝紫色闪烁光圈 */
                    int pmid = screenLeft + CELL_SIZE / 2;
                    int pmidy = screenTop + CELL_SIZE / 2;
                    int pradius = CELL_SIZE / 2 - 2;
                    uint16_t pColor = (g_portalFlash & 1) ? BLUE : MAGENTA;
                    /* 外圈 */
                    draw_circle_with_map(pmid, pmidy, pradius, pColor);
                    /* 内圈发光 */
                    fill_circle_fast(pmid, pmidy, pradius - 4, 
                                     (g_portalFlash & 2) ? BLUE : DARKBLUE);
                    /* 中心白点 */
                    draw_point_with_map(pmid, pmidy, WHITE);
                } else if (tile == TILE_SPIKE_DOWN) {
                    /* 地刺收起 - 灰色地板带4个小凹坑 */
                    fill_rect_with_map(screenLeft, screenTop,
                                       screenLeft + CELL_SIZE - 1,
                                       screenTop + CELL_SIZE - 1,
                                       GRAY);
                    /* 4个小凹坑（深灰色方块表示收起的刺孔）*/
                    {
                        int q = CELL_SIZE / 4;
                        int cs = 3; /* 凹坑大小 */
                        fill_rect_with_map(screenLeft + q - cs/2, screenTop + q - cs/2,
                                           screenLeft + q + cs/2, screenTop + q + cs/2, 0x6B6B);
                        fill_rect_with_map(screenLeft + 3*q - cs/2, screenTop + q - cs/2,
                                           screenLeft + 3*q + cs/2, screenTop + q + cs/2, 0x6B6B);
                        fill_rect_with_map(screenLeft + q - cs/2, screenTop + 3*q - cs/2,
                                           screenLeft + q + cs/2, screenTop + 3*q + cs/2, 0x6B6B);
                        fill_rect_with_map(screenLeft + 3*q - cs/2, screenTop + 3*q - cs/2,
                                           screenLeft + 3*q + cs/2, screenTop + 3*q + cs/2, 0x6B6B);
                    }
                } else if (tile == TILE_SPIKE_UP) {
                    /* 地刺弹出 - 灰色地板 + 红色三角尖刺 */
                    fill_rect_with_map(screenLeft, screenTop,
                                       screenLeft + CELL_SIZE - 1,
                                       screenTop + CELL_SIZE - 1,
                                       GRAY);
                    /* 4个向上的三角尖刺（用3个小方块模拟）*/
                    {
                        int q = CELL_SIZE / 4;
                        /* 顶部尖 */
                        fill_rect_with_map(screenLeft + q - 2, screenTop + q - 2,
                                           screenLeft + q + 2, screenTop + q - 2, RED);
                        fill_rect_with_map(screenLeft + q - 1, screenTop + q - 4,
                                           screenLeft + q + 1, screenTop + q - 2, RED);
                        fill_rect_with_map(screenLeft + q, screenTop + q - 5,
                                           screenLeft + q, screenTop + q - 2, RED);
                        /* 右侧尖 */
                        fill_rect_with_map(screenLeft + 3*q - 2, screenTop + q - 2,
                                           screenLeft + 3*q + 2, screenTop + q - 2, RED);
                        fill_rect_with_map(screenLeft + 3*q - 1, screenTop + q - 4,
                                           screenLeft + 3*q + 1, screenTop + q - 2, RED);
                        fill_rect_with_map(screenLeft + 3*q, screenTop + q - 5,
                                           screenLeft + 3*q, screenTop + q - 2, RED);
                        /* 下方尖 */
                        fill_rect_with_map(screenLeft + q - 2, screenTop + 3*q - 2,
                                           screenLeft + q + 2, screenTop + 3*q - 2, RED);
                        fill_rect_with_map(screenLeft + q - 1, screenTop + 3*q - 4,
                                           screenLeft + q + 1, screenTop + 3*q - 2, RED);
                        fill_rect_with_map(screenLeft + q, screenTop + 3*q - 5,
                                           screenLeft + q, screenTop + 3*q - 2, RED);
                        /* 左下尖 */
                        fill_rect_with_map(screenLeft + 3*q - 2, screenTop + 3*q - 2,
                                           screenLeft + 3*q + 2, screenTop + 3*q - 2, RED);
                        fill_rect_with_map(screenLeft + 3*q - 1, screenTop + 3*q - 4,
                                           screenLeft + 3*q + 1, screenTop + 3*q - 2, RED);
                        fill_rect_with_map(screenLeft + 3*q, screenTop + 3*q - 5,
                                           screenLeft + 3*q, screenTop + 3*q - 2, RED);
                    }
                } else if (tile == TILE_CHEST) {
                    /* 宝箱 - 关闭：棕色箱子 + 金色锁扣 */
                    int m = 3;
                    fill_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE - m - 1, 0x8400);
                    /* 箱盖（深棕色）*/
                    fill_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE / 2, 0x4A00);
                    /* 金色锁扣 */
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2,
                                       screenTop + CELL_SIZE/2 - 2,
                                       screenLeft + CELL_SIZE/2 + 2,
                                       screenTop + CELL_SIZE/2 + 2, YELLOW);
                } else if (tile == TILE_CHEST_OPEN) {
                    /* 宝箱 - 已打开：灰色空箱 */
                    int m = 3;
                    fill_rect_with_map(screenLeft + m, screenTop + CELL_SIZE/2,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE - m - 1, 0x6B6B);
                    fill_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE/2 - 1, 0x4208);
                    fill_rect_with_map(screenLeft + m + 1, screenTop + CELL_SIZE/2 + 1,
                                       screenLeft + CELL_SIZE - m - 2,
                                       screenTop + CELL_SIZE - m - 2, 0x2104);
                } else if (tile == TILE_CRATE) {
                    /* 箱子 - 未破坏：木色箱子 + 纹理 */
                    int m = 2;
                    /* 箱子底色（浅棕色）*/
                    fill_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE - m - 1, 0xD6A6);
                    /* 箱子边缘（深棕色）*/
                    draw_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE - m - 1, 0x8B40);
                    /* 箱子顶部条纹（模拟木纹）*/
                    fill_rect_with_map(screenLeft + m + 1, screenTop + m + 2,
                                       screenLeft + CELL_SIZE - m - 2, screenTop + m + 4, 0xB784);
                    fill_rect_with_map(screenLeft + m + 1, screenTop + m + 8,
                                       screenLeft + CELL_SIZE - m - 2, screenTop + m + 10, 0xB784);
                    /* 箱子中心铆钉（铁灰色）*/
                    int cx = screenLeft + CELL_SIZE / 2;
                    int cy = screenTop + CELL_SIZE / 2;
                    fill_rect_with_map(cx - 2, cy - 2, cx + 2, cy + 2, 0x6B6B);
                } else if (tile == TILE_CRATE_BROKEN) {
                    /* 箱子 - 已破坏：灰色碎块 */
                    int m = 2;
                    fill_rect_with_map(screenLeft + m, screenTop + m,
                                       screenLeft + CELL_SIZE - m - 1,
                                       screenTop + CELL_SIZE - m - 1, 0x6B6B);
                    /* 碎裂纹理（深灰色小块）*/
                    fill_rect_with_map(screenLeft + 4, screenTop + 4, screenLeft + 7, screenTop + 6, 0x4208);
                    fill_rect_with_map(screenLeft + 10, screenTop + 8, screenLeft + 12, screenTop + 10, 0x4208);
                    fill_rect_with_map(screenLeft + 6, screenTop + 12, screenLeft + 8, screenTop + 14, 0x4208);
                } else if (tile == TILE_TNT) {
                    /* TNT 炸药桶：深灰色底+红色炸药标识 */
                    int pad = 2;
                    fill_rect_with_map(screenLeft + pad, screenTop + pad,
                                       screenLeft + CELL_SIZE - pad - 1,
                                       screenTop + CELL_SIZE - pad - 1, 0x4208);
                    /* 红色桶身 */
                    fill_rect_with_map(screenLeft + pad, screenTop + pad + 2,
                                       screenLeft + CELL_SIZE - pad - 1,
                                       screenTop + CELL_SIZE - pad - 1 - 2, 0xF800);
                    /* 黄色引信顶部 */
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 1, screenTop + pad - 2,
                                       screenLeft + CELL_SIZE/2 + 1, screenTop + pad, YELLOW);
                    /* 黑色文字 T */
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/2,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/2 + 5, 0x0000);
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 4, screenTop + CELL_SIZE/2 + 1,
                                       screenLeft + CELL_SIZE/2 + 4, screenTop + CELL_SIZE/2 + 3, 0x0000);
                } else if (tile == TILE_POTION) {
                    /* 生命药水：红色药水瓶 */
                    int m = 2;
                    /* 瓶身（红色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/3, screenTop + CELL_SIZE/2,
                                       screenLeft + 2*CELL_SIZE/3, screenTop + CELL_SIZE - m - 1, RED);
                    /* 瓶口（白色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/3,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/2, WHITE);
                    /* 瓶盖（金色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 3, screenTop + CELL_SIZE/4,
                                       screenLeft + CELL_SIZE/2 + 3, screenTop + CELL_SIZE/3, YELLOW);
                    /* 药水反光（浅红色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 1, screenTop + CELL_SIZE/2 + 2,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/2 + 4, 0xF800);
                } else if (tile == TILE_ENERGY_POTION) {
                    /* 能量药水：蓝色药水瓶（与生命药水形状相同，蓝色）*/
                    int m = 2;
                    /* 瓶身（蓝色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/3, screenTop + CELL_SIZE/2,
                                       screenLeft + 2*CELL_SIZE/3, screenTop + CELL_SIZE - m - 1, CYAN);
                    /* 瓶口（白色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/3,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/2, WHITE);
                    /* 瓶盖（蓝色亮边）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 3, screenTop + CELL_SIZE/4,
                                       screenLeft + CELL_SIZE/2 + 3, screenTop + CELL_SIZE/3, CYAN);
                    /* 药水反光（亮蓝色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 1, screenTop + CELL_SIZE/2 + 2,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/2 + 4, 0x001F);
                } else if (tile == TILE_WEAPON_SHOTGUN) {
                    /* 霰弹枪：橙色长条形 */
                    /* 枪身（橙色）*/
                    fill_rect_with_map(screenLeft + 2, screenTop + CELL_SIZE/2 - 2,
                                       screenLeft + CELL_SIZE - 2, screenTop + CELL_SIZE/2 + 2, 0xF800);
                    /* 枪托（深橙色）*/
                    fill_rect_with_map(screenLeft + 2, screenTop + CELL_SIZE/2 - 3,
                                       screenLeft + 6, screenTop + CELL_SIZE/2 + 3, 0xD200);
                    /* 枪管（金属灰）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE - 5, screenTop + CELL_SIZE/2 - 1,
                                       screenLeft + CELL_SIZE - 2, screenTop + CELL_SIZE/2 + 1, 0x7BEF);
                } else if (tile == TILE_WEAPON_SMG) {
                    /* 冲锋枪：绿色矩形+弹匣 */
                    /* 枪身（绿色）*/
                    fill_rect_with_map(screenLeft + 2, screenTop + CELL_SIZE/2 - 2,
                                       screenLeft + CELL_SIZE - 4, screenTop + CELL_SIZE/2 + 2, 0x07E0);
                    /* 枪管（灰色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE - 5, screenTop + CELL_SIZE/2 - 1,
                                       screenLeft + CELL_SIZE - 2, screenTop + CELL_SIZE/2 + 1, 0x7BEF);
                    /* 弹匣（深绿色）*/
                    fill_rect_with_map(screenLeft + 4, screenTop + CELL_SIZE/2 + 3,
                                       screenLeft + 8, screenTop + CELL_SIZE - 2, 0x03E0);
                } else if (tile == TILE_WEAPON_LASER) {
                    /* 激光炮：青色圆形发射器+光束线 */
                    /* 发射器底座（深青色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/2 + 1,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE - 2, 0x07FF);
                    /* 发射口（青色圆）*/
                    lcd_draw_circle(screenLeft + CELL_SIZE/2, screenTop + CELL_SIZE/2, 3, CYAN);
                    /* 光束（青色线条）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2, screenTop + 1,
                                       screenLeft + CELL_SIZE/2 + 1, screenTop + CELL_SIZE/2 - 2, CYAN);
                    /* 光束高光（白色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 1, screenTop + 1,
                                       screenLeft + CELL_SIZE/2 + 1, screenTop + CELL_SIZE/2 - 3, WHITE);
                } else if (tile == TILE_WEAPON_STAFF) {
                    /* 法杖：紫色棒身+能量球 */
                    /* 棒身（紫色长条）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 1, screenTop + 2,
                                       screenLeft + CELL_SIZE/2 + 1, screenTop + CELL_SIZE - 2, 0xA000);
                    /* 棒头（紫色圆）*/
                    lcd_draw_circle(screenLeft + CELL_SIZE/2, screenTop + 3, 3, 0xA000);
                    /* 能量球（粉紫色发光）*/
                    lcd_draw_circle(screenLeft + CELL_SIZE/2, screenTop + 3, 2, 0xF81F);
                    /* 能量光芒（粉色短线）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 4, screenTop + 2,
                                       screenLeft + CELL_SIZE/2 - 3, screenTop + 3, 0xF81F);
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 3, screenTop + 2,
                                       screenLeft + CELL_SIZE/2 + 4, screenTop + 3, 0xF81F);
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 4, screenTop + 4,
                                       screenLeft + CELL_SIZE/2 - 3, screenTop + 5, 0xF81F);
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 3, screenTop + 4,
                                       screenLeft + CELL_SIZE/2 + 4, screenTop + 5, 0xF81F);
                } else if (tile == TILE_WEAPON_PISTOL) {
                    /* 手枪：小型灰色手枪，简洁设计 */
                    /* 枪身（深灰色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/2 - 1,
                                       screenLeft + CELL_SIZE/2 + 3, screenTop + CELL_SIZE/2 + 1, 0x7BEF);
                    /* 枪管（黑色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 + 3, screenTop + CELL_SIZE/2 - 1,
                                       screenLeft + CELL_SIZE/2 + 5, screenTop + CELL_SIZE/2, 0x4208);
                    /* 握把（深灰色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/2 + 1,
                                       screenLeft + CELL_SIZE/2, screenTop + CELL_SIZE/2 + 4, 0x6B6B);
                } else if (tile == TILE_WEAPON_ROCKET) {
                    /* 火箭炮：红色粗管+弹头 */
                    /* 管身（暗红色粗矩形）*/
                    fill_rect_with_map(screenLeft + 3, screenTop + CELL_SIZE/2 - 3,
                                       screenLeft + CELL_SIZE - 5, screenTop + CELL_SIZE/2 + 3, RED);
                    /* 弹头（亮橙色三角/矩形）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE - 6, screenTop + CELL_SIZE/2 - 2,
                                       screenLeft + CELL_SIZE - 2, screenTop + CELL_SIZE/2 + 2, 0xFC00);
                    /* 弹头尖端（黄色）*/
                    fill_rect_with_map(screenLeft + CELL_SIZE - 4, screenTop + CELL_SIZE/2 - 1,
                                       screenLeft + CELL_SIZE - 2, screenTop + CELL_SIZE/2 + 1, YELLOW);
                    /* 握把（深红色）*/
                    fill_rect_with_map(screenLeft + 3, screenTop + CELL_SIZE/2 + 3,
                                       screenLeft + 8, screenTop + CELL_SIZE - 2, 0x9000);
                } else if (tile == TILE_GACHAPON) {
                    /* 彩蛋机：金色球体机器，带投币口 */
                    int cx = screenLeft + CELL_SIZE / 2;
                    int cy = screenTop + CELL_SIZE / 2;
                    /* 机器主体（金色圆形）*/
                    lcd_draw_circle(cx, cy, 6, 0xFE00);  /* 金色边框 */
                    fill_circle_fast(cx, cy, 5, 0xFFE0);  /* 亮金色填充 */
                    /* 投币口（深棕色小矩形）*/
                    fill_rect_with_map(cx - 2, cy - 2, cx + 2, cy, 0x8B40);
                    /* 问号标志（紫色）*/
                    fill_rect_with_map(cx - 1, cy + 1, cx + 1, cy + 3, 0xA000);
                    draw_point_with_map(cx, cy + 1, 0xA000);
                } else if (tile == TILE_SHOP_LEFT) {
                    /* 商店左格：柜台 */
                    fill_rect_with_map(screenLeft, screenTop + CELL_SIZE/2,
                                       screenLeft + CELL_SIZE - 1, screenTop + CELL_SIZE - 1, 0x8B40);  /* 棕色柜台 */
                    fill_rect_with_map(screenLeft + 2, screenTop + CELL_SIZE/2 + 2,
                                       screenLeft + CELL_SIZE - 3, screenTop + CELL_SIZE/2 + 4, 0xD6A6);  /* 木纹 */
                    /* 柜台上的金币标志 */
                    lcd_draw_circle(screenLeft + CELL_SIZE/2, screenTop + CELL_SIZE/3, 4, YELLOW);
                } else if (tile == TILE_SHOP_RIGHT) {
                    /* 商店右格：柜台延续 */
                    fill_rect_with_map(screenLeft, screenTop + CELL_SIZE/2,
                                       screenLeft + CELL_SIZE - 1, screenTop + CELL_SIZE - 1, 0x8B40);  /* 棕色柜台 */
                    fill_rect_with_map(screenLeft + 2, screenTop + CELL_SIZE/2 + 2,
                                       screenLeft + CELL_SIZE - 3, screenTop + CELL_SIZE/2 + 4, 0xD6A6);  /* 木纹 */
                    /* 柜台上的商品展示 */
                    fill_rect_with_map(screenLeft + CELL_SIZE/2 - 2, screenTop + CELL_SIZE/3 - 2,
                                       screenLeft + CELL_SIZE/2 + 2, screenTop + CELL_SIZE/3 + 2, RED);
                }
            }
        }
    }
}

/* ==================== 玩家动画 ==================== */
#define ANIM_FRAME_MS     120     /* 每帧动画间隔(ms)，约8fps的腿交替 */
static uint8_t  g_playerDir = 0;  /* 0=下 1=上 2=左 3=右 */
static uint8_t  g_animFrame  = 0; /* 0/1 腿交替帧 */
static uint32_t g_lastAnimTick = 0;
static uint8_t  g_isMoving = 0;   /* 当前帧是否在移动 */

/* 获取玩家朝向（4方向，根据摇杆方向）*/
static void update_player_dir(void) {
    if (!joystickActive) { g_isMoving = 0; return; }
    g_isMoving = 1;
    float ax = (joystickDirX < 0) ? -joystickDirX : joystickDirX;
    float ay = (joystickDirY < 0) ? -joystickDirY : joystickDirY;
    if (ax > ay) {
        g_playerDir = (joystickDirX > 0) ? 3 : 2; /* 右/左 */
    } else {
        g_playerDir = (joystickDirY > 0) ? 0 : 1; /* 下/上 */
    }
}

/* 绘制像素小人（俯视角，元气骑士风格）
 * cx,cy = 屏幕中心人物位置
 * dir   = 朝向 0下1上2左3右
 * frame = 0站立 1左脚前 2右脚前 */
static void draw_player_sprite(int cx, int cy, uint8_t dir, uint8_t frame) {
    uint16_t skinColor = 0xFDB8;   /* 肤色 */
    uint16_t hairColor = 0x8400;   /* 深棕发色 */
    uint16_t clothColor = 0x051F;  /* 深蓝衣服 */
    uint16_t pantsColor = 0x4208;  /* 深灰裤子 */
    uint16_t shoeColor  = 0x8000;  /* 深红鞋子 */
    uint16_t eyeColor   = BLACK;
    
    int R = PLAYER_SIZE / 2; /* 半径11 */
    int headR = 6;            /* 头部半径 */
    int bodyTop, bodyBot;
    int legLen = 4;
    
    /* ===== 头部（圆形，所有方向一样）===== */
    int headY = cy - R + 2;   /* 头顶偏上 */
    fill_circle_fast(cx, headY, headR, skinColor);
    /* 头发 */
    if (dir == 1) {
        /* 朝上：头发覆盖整个头 */
        fill_circle_fast(cx, headY - 1, headR, hairColor);
        /* 露出后脑轮廓（肤色边框）*/
        draw_circle_with_map(cx, headY, headR, skinColor);
    } else if (dir == 0) {
        /* 朝下：刘海 */
        draw_rect_with_map(cx - 5, headY - 6, cx + 5, headY - 3, hairColor);
        /* 眼睛 */
        draw_point_with_map(cx - 3, headY - 1, eyeColor);
        draw_point_with_map(cx + 3, headY - 1, eyeColor);
    } else if (dir == 2) {
        /* 朝左 */
        draw_rect_with_map(cx - 6, headY - 5, cx - 1, headY + 1, hairColor);
        draw_point_with_map(cx - 3, headY - 1, eyeColor);
    } else {
        /* 朝右 */
        draw_rect_with_map(cx + 1, headY - 5, cx + 6, headY + 1, hairColor);
        draw_point_with_map(cx + 3, headY - 1, eyeColor);
    }
    
    /* ===== 身体 ===== */
    bodyTop = headY + headR - 1;
    bodyBot = bodyTop + 6;
    fill_rect_with_map(cx - 5, bodyTop, cx + 5, bodyBot, clothColor);
    
    /* ===== 腿部（带跑动动画）===== */
    if (frame == 0 || !g_isMoving) {
        /* 站立：两腿并拢 */
        fill_rect_with_map(cx - 3, bodyBot + 1, cx - 1, bodyBot + legLen, pantsColor);
        fill_rect_with_map(cx + 1, bodyBot + 1, cx + 3, bodyBot + legLen, pantsColor);
        /* 鞋子 */
        draw_point_with_map(cx - 2, bodyBot + legLen + 1, shoeColor);
        draw_point_with_map(cx + 2, bodyBot + legLen + 1, shoeColor);
    } else if (frame == 1) {
        /* 左脚前伸右脚后缩（俯视角：人朝y-方向跑时，腿往上伸）*/
        /* fwd表示"脚往屏幕哪个方向伸" */
        int fwdX = (dir == 2) ? -1 : (dir == 3) ? 1 : 0; /* 左移→腿上移 右移→腿下移 */
        int fwdY = (dir == 1) ? -1 : (dir == 0) ? 1 : 0; /* 上移→腿上移 下移→腿下移 */
        /* 左腿前伸 */
        fill_rect_with_map(cx - 3 + fwdX, bodyBot + 1 - fwdY, cx - 1 + fwdX, bodyBot + legLen - fwdY, pantsColor);
        draw_point_with_map(cx - 2 + fwdX, bodyBot + legLen + 1 - fwdY, shoeColor);
        /* 右腿后缩 */
        fill_rect_with_map(cx + 1 - fwdX, bodyBot + 1 + fwdY, cx + 3 - fwdX, bodyBot + legLen - 1 + fwdY, pantsColor);
        draw_point_with_map(cx + 2 - fwdX, bodyBot + legLen - fwdY, shoeColor);
    } else {
        /* 右脚前伸左脚后缩 */
        int fwdX = (dir == 2) ? -1 : (dir == 3) ? 1 : 0;
        int fwdY = (dir == 1) ? -1 : (dir == 0) ? 1 : 0;
        /* 右腿前伸 */
        fill_rect_with_map(cx + 1 + fwdX, bodyBot + 1 - fwdY, cx + 3 + fwdX, bodyBot + legLen - fwdY, pantsColor);
        draw_point_with_map(cx + 2 + fwdX, bodyBot + legLen + 1 - fwdY, shoeColor);
        /* 左腿后缩 */
        fill_rect_with_map(cx - 3 - fwdX, bodyBot + 1 + fwdY, cx - 1 - fwdX, bodyBot + legLen - 1 + fwdY, pantsColor);
        draw_point_with_map(cx - 2 - fwdX, bodyBot + legLen - fwdY, shoeColor);
    }
}

/* ==================== 绘制玩家 ==================== */
static void draw_player(void) {
    int centerX = LCD_WIDTH / 2;
    int centerY = LCD_HEIGHT / 2 - 40;
    draw_player_sprite(centerX, centerY, g_playerDir, g_animFrame);
}

/* ==================== 绘制摇杆 ==================== */
static void draw_joystick(void) {
    draw_circle_with_map(joystickCenterX, joystickCenterY, JOYSTICK_RADIUS, WHITE);
    fill_circle_fast(knobX, knobY, JOYSTICK_KNOB_RADIUS, WHITE);
}

/* ==================== 玩家属性 ==================== */
#define HP_MAX       5
#define DEF_MAX      6
#define ENERGY_MAX   180   /* 能量上限 */
#define ENERGY_DRAIN_LASER  2  /* 激光炮每0.5秒消耗能量 */
#define ENERGY_COST_STAFF   3  /* 法杖每次消耗能量 */

/* ==================== 武器定义 ==================== */
static uint8_t g_currentWeapon = WEAPON_PISTOL;  /* 当前武器，默认手枪 */

/* 激光炮状态 */
static uint8_t  g_laserActive = 0;    /* 激光状态：0=关闭，1=激活，2=充能中 */
static float   g_laserDirX = 0.0f;   /* 激光方向 */
static float   g_laserDirY = 0.0f;
static float   g_laserStartX = 0.0f; /* 激光起始世界坐标（按下时的玩家位置）*/
static float   g_laserStartY = 0.0f;
static float   g_laserEndX = 0.0f;   /* 激光末端世界坐标 */
static float   g_laserEndY = 0.0f;
static uint32_t g_laserStartTick = 0; /* 激光伤害计时（最后造成伤害的时间）*/
static uint32_t g_laserChargeTick = 0; /* 激光充能计时（按下KEY0的时刻）*/
static uint32_t g_laserEnergyTick = 0; /* 激光能量扣减计时（每0.5秒扣一次）*/

/* 冲锋枪连发计时 */
static uint32_t g_smgLastFire = 0;

/* 法杖发射冷却计时（避免连发）*/
static uint32_t g_staffLastFire = 0;
#define STAFF_FIRE_COOLDOWN 300  /* 法杖冷却时间300ms */

/* 法杖子弹速度比普通子弹慢 */
#define STAFF_BULLET_SPEED  3.0f   /* 法杖子弹速度（比普通子弹慢，大慢弹）*/
#define STAFF_BULLET_RADIUS  5      /* 法杖子弹半径（像素，比普通子弹大）*/

/* ==================== 火箭炮定义 ==================== */
#define ROCKET_SPEED       5.0f   /* 火箭弹飞行速度（比普通子弹慢）*/
#define ROCKET_COOLDOWN    1200   /* 火箭炮冷却时间1200ms（攻速较慢）*/
#define ROCKET_EXPLODE_DMG 5      /* 火箭爆炸伤害（=TNT）*/
#define ROCKET_RADIUS      1      /* 火箭爆炸半径：周围1格（3×3，=TNT）*/
#define ENERGY_COST_ROCKET  5     /* 火箭炮每次消耗能量 */
static uint32_t g_rocketLastFire = 0;  /* 火箭炮上次发射时间 */

static uint8_t  g_hp  = HP_MAX;
static uint8_t  g_def = DEF_MAX;
static uint16_t g_energy = ENERGY_MAX;  /* 能量 */
static uint16_t g_gold = 0;           /* 金币数量 */
static uint32_t g_lastDamageTick = 0;   /* 上次受伤时间，用于护盾恢复计时 */

/* ==================== 地面物品定义 ==================== */
#define MAX_GROUND_ITEMS  10

typedef struct {
    float x, y;
    uint8_t type;       /* 物品类型：0=无，1=霰弹枪 */
    uint8_t active;
} GroundItem;

static GroundItem g_groundItems[MAX_GROUND_ITEMS];
static int g_groundItemCount = 0;

/* 将当前武器类型转换为对应的地面物品 TILE 类型 */
static uint8_t weapon_to_tile(uint8_t weapon) {
    switch (weapon) {
        case WEAPON_PISTOL:    return TILE_WEAPON_PISTOL;  /* 手枪也掉落 */
        case WEAPON_SHOTGUN:   return TILE_WEAPON_SHOTGUN;
        case WEAPON_SMG:       return TILE_WEAPON_SMG;
        case WEAPON_LASER:     return TILE_WEAPON_LASER;
        case WEAPON_STAFF:     return TILE_WEAPON_STAFF;
        case WEAPON_ROCKET:    return TILE_WEAPON_ROCKET;
        default:               return TILE_FLOOR;
    }
}

/* ==================== 怪物类型定义 ==================== */
#define MONSTER_TYPE_A     1       /* A类：近战型，3滴血，伤害1 */
#define MONSTER_TYPE_B     2       /* B类：巨型远程子弹，5滴血，伤害4 */
#define MONSTER_TYPE_C     3       /* C类：大量远程子弹，2滴血，伤害2 */
#define MONSTER_TYPE_D     4       /* D类：宝箱守卫，环形子弹，10滴血，伤害2 */
#define MONSTER_TYPE_BOSS  5       /* BOSS：三种攻击，50滴血 */
#define MONSTER_TYPE_BOSS2 6       /* BOSS2：五种攻击，100滴血（第10关） */

#define MONSTER_A_HP      3
#define MONSTER_B_HP      5
#define MONSTER_C_HP      2
#define MONSTER_D_HP      10      /* D类宝箱守卫血量10滴 */
#define MONSTER_BOSS_HP   50      /* BOSS血量50滴 */
#define MONSTER_BOSS2_HP 100      /* BOSS2血量100滴 */
#define MONSTER_A_DMG     1       /* A类近战伤害 */
#define MONSTER_B_DMG     4       /* B类巨型子弹伤害 */
#define MONSTER_C_DMG     2       /* C类子弹伤害 */
#define MONSTER_D_DMG     2       /* D类环形子弹伤害 */
#define MONSTER_BOSS_DMG1 3       /* BOSS第一种子弹伤害（巨大子弹） */
#define MONSTER_BOSS_DMG2 2       /* BOSS第二种子弹伤害（快速小子弹） */
#define MONSTER_BOSS_DMG3 5       /* BOSS第三种子弹伤害（最强子弹） */
#define MONSTER_BOSS2_DMG4 (ROCKET_EXPLODE_DMG)  /* BOSS2火箭弹伤害=爆炸伤害5 */

/* ==================== 箱子系统 ==================== */
#define MAX_CRATES        20      /* 最大箱子数 */
#define CRATE_HP          2       /* 箱子血量：2滴 */
#define CRATE_DROP_RATE   30      /* 箱子掉落药水总概率：30% */
#define CRATE_ENERGY_CHANCE 15    /* 掉能量药水概率：15%（另一半15%掉生命药水） */
#define ENERGY_PER_KILL   10      /* 击杀敌人给10能量 */
#define ENERGY_PER_LEVEL  15      /* 通过每层地牢额外给15能量 */
#define ENERGY_POTION_RESTORE 50  /* 能量药水恢复50点 */
#define CRATES_PER_ROOM   7       /* 每个房间箱子数量 */

/* ==================== TNT 炸药桶系统 ==================== */
#define MAX_TNTS          30      /* 最大TNT数 */
#define TNT_HP            2       /* TNT血量：2滴 */
#define TNT_EXPLODE_DMG   5       /* 爆炸伤害（对所有生物）*/
#define TNT_RADIUS        1       /* 爆炸半径：周围1格（3×3共9格，含中心）*/

/* TNT 爆炸状态机 */
typedef enum {
    TNT_IDLE = 0,     /* 待命：显示为炸药桶 */
    TNT_EXPLODING,    /* 爆炸中：播放动画 */
    TNT_DESTROYED     /* 已销毁：格子恢复地板 */
} TntState;

/* TNT 结构体 */
typedef struct {
    int x, y;               /* 地图格子坐标 */
    TntState state;         /* 当前状态 */
    int     hp;             /* 当前血量（默认2滴）*/
    uint32_t explodeStart;  /* 爆炸开始时间戳（用于动画计时）*/
} TNT;

static TNT         g_tnts[MAX_TNTS];
static int        g_tntCount = 0;
static int        g_tntAnimActive = 0;  /* 是否有TNT在爆炸动画中 */

/* ========== TNT 爆炸动画渲染（辅助圆绘制）============ */
/*
 * 在格子像素坐标(sx,sy)处绘制半径r的实心圆
 */
static void fill_circle_map(int sx, int sy, int r, uint16_t col) {
    int cr2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py = sy + dy;
        if (py < 0 || py >= LCD_HEIGHT) continue;
        int dxMax = (int)(sqrtf((float)(cr2 - dy*dy)) + 0.5f);
        int px0 = sx - dxMax;
        int px1 = sx + dxMax;
        if (px0 < 0) px0 = 0;
        if (px1 >= LCD_WIDTH) px1 = LCD_WIDTH - 1;
        if (px0 <= px1) fill_rect_with_map(px0, py, px1, py, col);
    }
}

/*
 * 绘制所有TNT爆炸动画（每帧调用，渲染在Dungeon层之上）
 * 爆炸持续450ms，分5帧：
 *   帧0（0~90ms）：黄色亮点+白色闪光
 *   帧1（90~180ms）：金黄圆r=5px
 *   帧2（180~270ms）：橙红圆r=7px
 *   帧3（270~360ms）：大红圆r=9px+黄色内核
 *   帧4（360~450ms）：白烟淡出r=6px
 */
#define TNT_ANIM_TOTAL  1000  /* 爆炸动画持续1秒，玩家能看到 */
#define TNT_ANIM_FRAMES  5
static void draw_tnt_explosions(void) {
    if (!g_tntAnimActive) return;
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < MAX_TNTS; i++) {
        TNT *t = &g_tnts[i];
        if (t->state != TNT_EXPLODING) continue;

        uint32_t elapsed = now - t->explodeStart;
        if (elapsed >= TNT_ANIM_TOTAL) {
            t->state = TNT_DESTROYED;
            g_dungeon.map[t->y][t->x] = TILE_FLOOR;
            continue;
        }

        /* 爆炸动画：3×3 冲击波扩散，每帧半径×颜色随时间变化 */
        int frames = 7;  /* 帧数（整数）*/
        int frame = (int)(elapsed * frames / TNT_ANIM_TOTAL);
        if (frame < 0) frame = 0;
        if (frame >= frames) frame = frames - 1;

        /* 3×3 爆炸区域的屏幕坐标（从中心格偏移） */
        int centerSX = (int)((t->x + 0.5f - playerX) * CELL_SIZE) + LCD_WIDTH / 2;
        int centerSY = (int)((t->y + 0.5f - playerY) * CELL_SIZE) + (LCD_HEIGHT / 2 - 40);

        /* 帧对应：0=小火球 1=扩散中圈 2=中圈完成 3=扩散外圈 4=外圈完成 5=最强冲击 6=渐隐 */
        uint16_t colors[7]  = { YELLOW, 0xF800, 0xF800, 0xF7E0, 0xFF00, WHITE, 0xF81F };
        int      radii[7]  = {  6,      12,     18,     24,     30,     36,    0      };

        uint16_t col = colors[frame];
        int      r   = radii[frame];

        if (r > 0) {
            /* 3×3 区域逐格绘制大圆（确保覆盖所有9格） */
            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    int gx = t->x + ox;
                    int gy = t->y + oy;
                    if (gx < 0 || gx >= g_dungeon.width ||
                        gy < 0 || gy >= g_dungeon.height) continue;
                    /* 格的屏幕中心像素 */
                    int sx = (int)((gx + 0.5f - playerX) * CELL_SIZE) + LCD_WIDTH / 2;
                    int sy = (int)((gy + 0.5f - playerY) * CELL_SIZE) + (LCD_HEIGHT / 2 - 40);
                    fill_circle_map(sx, sy, r, col);
                }
            }
            /* 中心额外加亮（最外圈帧时叠一层白色）*/
            if (frame >= 4) {
                fill_circle_map(centerSX, centerSY, r - 2, WHITE);
            }
        }
    }

    /* 检查是否还有活跃爆炸 */
    g_tntAnimActive = 0;
    for (int i = 0; i < MAX_TNTS; i++) {
        if (g_tnts[i].state == TNT_EXPLODING) { g_tntAnimActive = 1; break; }
    }
}

/* 箱子结构体 */
typedef struct {
    int x, y;            /* 位置 */
    int alive;           /* 1=存活 0=已破坏 */
    int hp;              /* 当前血量 */
} Crate;

static Crate g_crates[MAX_CRATES];
static int g_crateCount = 0;

/* ========== 技能系统 ========== */
#define MAX_SKILLS       10      /* 最多10个技能 */
#define SKILL_HP_MAX     3       /* 血量+技能最大层数 */
#define SKILL_DEF_MAX    3       /* 防御+技能最大层数 */

/* 技能类型 */
typedef enum {
    SKILL_NONE = 0,
    SKILL_HP_UP,          /* 1. 血量上限+1 */
    SKILL_DEF_UP,         /* 2. 防御上限+1 */
    SKILL_ATK_UP,         /* 3. 攻击力+1 */
    SKILL_DUAL_WIELD,     /* 4. 双武器（前后同时射击）*/
    SKILL_STAFF_SPEED,    /* 5. 法杖类武器攻速×1.5 */
} SkillType;

/* 5x5 技能图标（透明色0x0000）*/
static const uint16_t SKILL_ICONS[6][25] = {
    /* SKILL_NONE */
    {0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0},
    /* SKILL_HP_UP: 红色十字（+）*/
    {0,0,RED,0,0, 0,0,RED,0,0, RED,RED,RED,RED,RED,
     0,0,RED,0,0, 0,0,RED,0,0},
    /* SKILL_DEF_UP: 绿色盾牌（简化）*/
    {0,0,GREEN,0,0, 0,GREEN,GREEN,GREEN,0, 0,GREEN,GREEN,GREEN,0,
     0,GREEN,GREEN,GREEN,0, 0,0,GREEN,0,0},
    /* SKILL_ATK_UP: 橙色星形 (0xFC00=RGB565橙色) */
    {0,0,0xFC00,0,0, 0xFC00,0xFC00,0xFC00,0xFC00,0xFC00,
     0,0xFC00,0xFC00,0xFC00,0, 0,0,0xFC00,0,0, 0,0,0,0,0},
    /* SKILL_DUAL_WIELD: 青色双箭头（←→）*/
    {0,0,0,0,0, CYAN,0,0,0,CYAN, 0,CYAN,CYAN,CYAN,0,
     CYAN,0,0,0,CYAN, 0,0,0,0,0},
    /* SKILL_STAFF_SPEED: 紫色闪电 (0xA000=RGB565紫色) */
    {0,0,0,0xA000,0, 0,0,0xA000,0,0, 0,0xA000,0xA000,0xA000,0,
     0,0,0xA000,0,0, 0xA000,0xA000,0,0,0},
};

/* 技能名称（纯ASCII，兼容Keil GBK环境）*/
static const char* SKILL_NAMES[6] = {
    "None", "HP+", "DEF+", "ATK+", "Dual", "StaffSpd"
};

/* ========== 技能追踪变量 ========== */
static int g_skillHpUp       = 0;   /* 血量+层数 */
static int g_skillDefUp      = 0;   /* 防御+层数 */
static int g_skillAtkUp      = 0;   /* 攻击力+层数 */
static int g_skillDualWield  = 0;   /* 双武器：0=关，1=开 */
static int g_skillStaffSpeed = 0;   /* 法杖攻速+层数 */
static int g_skillCount      = 0;   /* 当前已选技能总数 */
static int g_skillOptions[3];       /* 本次可选择的3个技能ID */

/* ========== 技能效果辅助函数 ========== */
#define get_max_hp()      (HP_MAX + g_skillHpUp)
#define get_max_def()     (DEF_MAX + g_skillDefUp)
#define get_atk_bonus()   (1 + g_skillAtkUp)
#define get_laser_dmg()   (2 + g_skillAtkUp)
/* 法杖攻速：基础500ms / 1.5^层数 */
static int get_staff_cooldown(void) {
    if (g_skillStaffSpeed == 0) return 500;
    if (g_skillStaffSpeed == 1) return 333;
    if (g_skillStaffSpeed == 2) return 222;
    return 148;
}

/* ========== 技能选择界面 ========== */
/* 生成3个随机技能选项（从5种中选，不含NONE）*/
static void generate_skill_options(void) {
    int avail[5] = {SKILL_HP_UP, SKILL_DEF_UP, SKILL_ATK_UP, SKILL_DUAL_WIELD, SKILL_STAFF_SPEED};
    for (int i = 0; i < 3; i++)
        g_skillOptions[i] = avail[dungeon_rand(&g_dungeon, 5)];
}

/* 绘制技能选择界面（3个选项横向排列）*/
static void draw_skill_select_ui(int sel) {
    lcd_fill(0, 40, LCD_WIDTH - 1, LCD_HEIGHT - 1, BLACK);
    lcd_show_string(LCD_WIDTH/2 - 50, 50, 120, 16, 16, "SELECT SKILL", WHITE);
    char buf[24];
    sprintf(buf, "%d/%d Skills", g_skillCount, MAX_SKILLS);
    lcd_show_string(LCD_WIDTH/2 - 40, 68, 100, 16, 16, buf, 0x07E0);

    int cw = 90, ch = 160, gap = 10;
    int total = cw * 3 + gap * 2;
    int sx0 = (LCD_WIDTH - total) / 2;
    int sy0 = 90;

    for (int i = 0; i < 3; i++) {
        int sx = sx0 + i * (cw + gap);
        SkillType st = (SkillType)g_skillOptions[i];
        uint16_t border = (i == sel) ? YELLOW : 0x421E;
        uint16_t bg = (i == sel) ? 0x001A : BLACK;
        lcd_fill(sx, sy0, sx + cw - 1, sy0 + ch - 1, bg);
        lcd_draw_rectangle(sx, sy0, sx + cw - 1, sy0 + ch - 1, border);

        /* 技能图标（5x5放大2x）*/
        const uint16_t* ic = SKILL_ICONS[st];
        int ix = sx + (cw - 20) / 2, iy = sy0 + 8;
        for (int py = 0; py < 5; py++)
            for (int px = 0; px < 5; px++) {
                uint16_t col = ic[py*5+px];
                if (col != 0x0000) {
                    lcd_fill(ix+px*2,   iy+py*2,   ix+px*2+1,   iy+py*2+1,   col);
                    lcd_fill(ix+px*2,   iy+py*2+1, ix+px*2+1,   iy+py*2+2,   col);
                }
            }

        /* 名称 */
        lcd_show_string(sx + 4, iy + 24, cw - 8, 16, 16, (char*)SKILL_NAMES[st], WHITE);

        /* 层数 */
        int lv = 0, maxLv = 99;
        if (st == SKILL_HP_UP)      { lv = g_skillHpUp;      maxLv = SKILL_HP_MAX; }
        else if (st == SKILL_DEF_UP) { lv = g_skillDefUp;     maxLv = SKILL_DEF_MAX; }
        else if (st == SKILL_ATK_UP) { lv = g_skillAtkUp; }
        else if (st == SKILL_DUAL_WIELD) { lv = g_skillDualWield; maxLv = 1; }
        else if (st == SKILL_STAFF_SPEED) { lv = g_skillStaffSpeed; }
        sprintf(buf, "%d/%d", lv, maxLv);
        lcd_show_string(sx + 4, iy + 42, cw - 8, 16, 16, buf, 0x07E0);

        /* 效果 */
        if (st == SKILL_HP_UP)      sprintf(buf, "+1 HP %d->%d", get_max_hp(), get_max_hp()+1);
        else if (st == SKILL_DEF_UP) sprintf(buf, "+1 DEF %d->%d", get_max_def(), get_max_def()+1);
        else if (st == SKILL_ATK_UP) sprintf(buf, "DMG %d->%d", get_atk_bonus(), get_atk_bonus()+1);
        else if (st == SKILL_DUAL_WIELD) sprintf(buf, "Shoot Back!");
        else sprintf(buf, "Staff CD x1.5");
        lcd_show_string(sx + 4, iy + 60, cw - 8, 16, 16, buf, 0xF800);

        /* 选中指示 */
        if (i == sel) lcd_show_string(sx + 4, sy0 + ch - 20, 40, 16, 16, ">>>", YELLOW);
        else lcd_show_string(sx + 4, sy0 + ch - 20, 40, 16, 16, "   ", BLACK);
    }

    lcd_show_string(20, LCD_HEIGHT - 50, 140, 16, 16, "Swipe: Select", 0x421E);
    lcd_show_string(20, LCD_HEIGHT - 30, 140, 16, 16, "KEY0: Confirm", 0x421E);
}

/* 技能选择主函数：技能满则跳过，返回1=已选，0=跳过 */
static int do_skill_selection(void) {
    if (g_skillCount >= MAX_SKILLS) return 0;

    generate_skill_options();

    int sel = 1;  /* 默认选中中间 */
    int16_t lastTX = -1;  /* 上次触摸X坐标，-1=未触摸 */
    uint32_t lastMoveMs = 0;
    #define MOVE_THRESH 30  /* 滑动超过30px才算切换 */
    #define MOVE_DEBOUNCE 300
    char buf[24];

    /* 等待KEY0松开（防止误触）*/
    while (KEY0) delay_ms(10);
    delay_ms(200);

    while (1) {
        uint32_t now = HAL_GetTick();

        /* 触摸屏滑动检测：左划=左移，右划=右移 */
        tp_dev.scan(0);
        if (tp_dev.sta & TP_PRES_DOWN) {
            int16_t tx = (int16_t)tp_dev.x[0];
            (void)tp_dev.y[0];  /* Y坐标暂不使用，保留兼容性 */
            if (lastTX >= 0 && now - lastMoveMs > MOVE_DEBOUNCE) {
                int dx = tx - lastTX;
                if (dx < -MOVE_THRESH && sel > 0) { sel--; lastMoveMs = now; }
                else if (dx > MOVE_THRESH && sel < 2) { sel++; lastMoveMs = now; }
            }
            lastTX = tx;
        } else {
            lastTX = -1;  /* 松开了 */
        }

        /* KEY0 按下 = 确认选择 */
        if (KEY0) {
            while (KEY0) delay_ms(10);
            delay_ms(100);

            SkillType st = (SkillType)g_skillOptions[sel];
            int applied = 0;

            if (st == SKILL_HP_UP && g_skillHpUp < SKILL_HP_MAX) {
                g_skillHpUp++; applied = 1;
                if (g_hp < get_max_hp()) g_hp++;  /* 立即补满新加的血量 */
            } else if (st == SKILL_DEF_UP && g_skillDefUp < SKILL_DEF_MAX) {
                g_skillDefUp++; applied = 1;
                if (g_def < get_max_def()) g_def++;  /* 立即补满新加的护盾 */
            } else if (st == SKILL_ATK_UP) {
                g_skillAtkUp++; applied = 1;
            } else if (st == SKILL_DUAL_WIELD && g_skillDualWield < 1) {
                g_skillDualWield = 1; applied = 1;
            } else if (st == SKILL_STAFF_SPEED) {
                g_skillStaffSpeed++; applied = 1;
            }

            if (applied) {
                g_skillCount++;
                lcd_fill(0, LCD_HEIGHT/2 - 20, LCD_WIDTH-1, LCD_HEIGHT/2+20, BLACK);
                sprintf(buf, "GOT: %s!", SKILL_NAMES[st]);
                lcd_show_string(LCD_WIDTH/2-50, LCD_HEIGHT/2, 120, 16, 16, buf, YELLOW);
                delay_ms(1000);
                return 1;
            }

            /* 技能已达上限 */
            lcd_fill(0, LCD_HEIGHT/2 + 20, LCD_WIDTH-1, LCD_HEIGHT/2+40, BLACK);
            lcd_show_string(LCD_WIDTH/2-60, LCD_HEIGHT/2+25, 140, 16, 16, "Max Level!", RED);
            delay_ms(600);
        }

        draw_skill_select_ui(sel);
        delay_ms(30);
    }
}

/* ==================== 怪物系统 ==================== */
#define MAX_MONSTERS       25      /* 增加最大怪物数（传送门房间15个+BOSS+其他房间）*/
#define MAX_MBULLETS       30
#define MONSTER_SPEED      2.0f    /* 怪物移动速度(格/秒) */
#define MELEE_INTERVAL     500     /* 近战攻击间隔(ms) */
#define RANGED_INTERVAL_B  1200    /* B类远程攻击间隔(较慢) */
#define RANGED_INTERVAL_C  800     /* C类远程攻击间隔(较快) */
#define RANGED_INTERVAL_D  500     /* D类环形子弹间隔(0.5秒) */
#define BOSS_INTERVAL1    1500    /* BOSS第一招间隔(巨大子弹，1.5秒) */
#define BOSS_INTERVAL2    800     /* BOSS第二招间隔(快速小子弹，0.8秒) */
#define BOSS_INTERVAL3    2000    /* BOSS第三招间隔(最强子弹，2秒) */
#define BOSS2_INTERVAL4   2500    /* BOSS2第四招间隔(双向火箭弹，2.5秒) */
#define BOSS2_INTERVAL5   4000    /* BOSS2第五招间隔(机枪扫射，4秒) */
#define BOSS2_MG_DURATION 1500    /* BOSS2机枪扫射持续时间(1.5秒) */
#define BOSS2_MG_RATE     100     /* BOSS2机枪射速(ms/发，和SMG一样) */
#define MBULLET_SPEED      5.0f    /* 怪物子弹速度 */
#define MBULLET_SPEED_B    2.0f    /* B类巨型子弹速度更快 */
#define MBULLET_SPEED_C    3.0f    /* C类大量子弹速度稍慢 */
#define PLAYER_BULLET_DMG  1       /* 玩家子弹对怪物的伤害 */

/* ==================== 房间类型定义 ==================== */
#define ROOM_TYPE_INIT     0       /* 初始房间：啥也没有 */
#define ROOM_TYPE_BATTLE   1       /* 战斗房间：三种怪物 */
#define ROOM_TYPE_CHEST    2       /* 宝箱房间：宝箱+守卫 */
#define ROOM_TYPE_PORTAL  3       /* 传送房间：地刺+传送门 */

typedef struct {
    float x, y;           /* 世界坐标 */
    int alive;            /* 1=存活 0=死亡 */
    int hp;               /* 当前血量 */
    int type;             /* 怪物类型：1=A类 2=B类 3=C类 4=D类 5=BOSS */
    int room_idx;         /* 所属房间索引 */
    int rx1, ry1, rx2, ry2; /* 房间地板边界(限制移动范围) */
    uint32_t lastMelee;   /* 上次近战时间 */
    uint32_t lastRanged;  /* 上次远程时间（A类/D类/BOSS第一招） */
    uint32_t lastRanged2; /* BOSS第二招时间 */
    uint32_t lastRanged3; /* BOSS第三招时间 */
    uint32_t lastRanged4; /* BOSS2第四招时间（双向火箭弹） */
    uint32_t lastRanged5; /* BOSS2第五招时间（持续机枪扫射） */
    uint32_t mgStartTick;  /* BOSS2机枪扫射开始时刻（0=未在扫射）*/
    int dir;              /* 朝向 0下1上2左3右 */
    uint32_t lastAnimTick;
    int animFrame;
} Monster;

static Monster g_monsters[MAX_MONSTERS];
static int g_monsterCount = 0;

/* 怪物子弹（区分B类、C类、D类和BOSS类） */
typedef struct {
    float x, y;
    float dx, dy;
    int active;
    /* 子弹类型：
     * 2=B类巨型子弹(高伤害)
     * 3=C类子弹
     * 4=D类子弹
     * 6=BOSS巨大子弹(伤害3)
     * 7=BOSS快速小子弹(伤害2)
     * 8=BOSS最强子弹(伤害5，会分裂) */
    int type;
    int splitTick;  /* BOSS第三种子弹的分裂计时 */
} MBullet;

static MBullet g_mbullets[MAX_MBULLETS];

/* 怪物专用碰撞：无视地刺（可以进入地刺范围且不受伤害），不能穿墙/门/出房间 */
static int monster_walkable(int mx, int my) {
    if (mx < 0 || mx >= g_dungeon.width || my < 0 || my >= g_dungeon.height) return 0;
    uint8_t t = g_dungeon.map[my][mx];
    /* 地板、地刺、TNT都可以走（怪物走上TNT会引爆，地刺不受伤）*/
    return (t == TILE_FLOOR || t == TILE_SPIKE_UP || t == TILE_SPIKE_DOWN || t == TILE_TNT);
}

/* 玩家受伤：先扣护盾，护盾不足再扣血 */
static void player_take_damage(uint8_t dmg) {
    if (dmg == 0) return;
    g_lastDamageTick = HAL_GetTick();
    if (g_def > 0) {
        if (g_def >= dmg) {
            g_def -= dmg;
        } else {
            /* 护盾不够，剩余伤害转给血量 */
            g_hp -= (dmg - g_def);
            g_def = 0;
        }
    } else {
        if (g_hp >= dmg) g_hp -= dmg;
        else g_hp = 0;
    }
}

/* 护盾自动恢复：受伤后每DEF_REGEN_MS恢复1格 */
static void update_def_regen(uint32_t now) {
    if (g_def >= get_max_def() || g_hp == 0) return;
    if (g_lastDamageTick == 0) return;  /* 从没受过伤 */
    if (now - g_lastDamageTick >= DEF_REGEN_MS) {
        g_lastDamageTick += DEF_REGEN_MS; /* 累加计时，不重置 */
        g_def++;
    }
}

/* 地刺伤害检测：每个地刺格子独立，踩到新格子才受伤一次 */
static void check_spike_damage(void) {
    if (!g_spikesUp) return;
    int px = (int)(playerX + 0.5f);
    int py = (int)(playerY + 0.5f);
    if (px < 0 || px >= DUNGEON_COLS || py < 0 || py >= DUNGEON_ROWS) return;
    if (g_dungeon.map[py][px] == TILE_SPIKE_UP) {
        /* 只有跟上次受伤的格子不同才受伤（同格站着不重复扣）*/
        if (px != g_lastSpikeHitX || py != g_lastSpikeHitY) {
            player_take_damage(SPIKE_DMG);
            g_lastSpikeHitX = (int8_t)px;
            g_lastSpikeHitY = (int8_t)py;
        }
    }
}

/* ==================== 顶部HUD（y=0..Y_OFFSET-1）==================== */
/* 20px高，分上下两行：
 *   y=1..7:   HP(红5块) + DEF(绿6块)，每块6x6，间距2px
 *   y=12..17: 左侧EXP(黄)，右侧金币(金)
 */
/* 3x5 点阵数字，每个"点"放大 scale 倍，黑底彩字 */
static void draw_num_at(int x, int y, int num, uint16_t fg, uint16_t bg) {
    char buf[6]; int len = 0;
    if (num == 0) { buf[len++] = '0'; }
    else { while (num > 0) { buf[len++] = '0' + num % 10; num /= 10; }
           for (int i = 0; i < len / 2; i++) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; } }
    static const uint8_t F[][5] = {
        {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},
        {5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},
        {7,5,7,5,7},{7,5,7,1,7}
    };
    int s = 3, gap = 1, dw = 9, dh = 15;
    lcd_fill(x, y, x + len * (dw + gap) - gap - 1, y + dh - 1, bg);
    int cx = x;
    for (int d = 0; d < len; d++) {
        int ch = buf[d] - '0';
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (F[ch][r] & (4 >> c))
                    lcd_fill(cx+c*s, y+r*s, cx+c*s+s-1, y+r*s+s-1, fg);
        cx += dw + gap;
    }
}

static void draw_hud(void) {
    int i, x;
    int bw = 6, bh = 6, gap = 2;

    /* 清黑HUD区域（y=0..19，直接写LCD，不经过pixelMap）*/
    lcd_fill(0, 0, LCD_WIDTH - 1, Y_OFFSET - 1, BLACK);

    /* 上半行: HP */
    x = 2;
    for (i = 0; i < get_max_hp(); i++) {
        uint16_t c = (i < g_hp) ? RED : 0x4208;
        lcd_fill(x, 1, x + bw - 1, 1 + bh - 1, c);
        x += bw + gap;
    }

    /* 技能图标区：HP和DEF之间，每个技能一个5x5图标 */
    {
        int iconX = x + 4;
        int iconY = 2;
        int iconScale = 2;  /* 每像素放大2倍 */
        int iconW = 5 * iconScale;
        /* iconH 不再需要单独存储 */
        int iconGap = 2;
        /* 统计各技能图标数量 */
        int n_hp   = g_skillHpUp;
        int n_def  = g_skillDefUp;
        int n_atk  = g_skillAtkUp;
        int n_du   = g_skillDualWield;
        int n_ss   = g_skillStaffSpeed;
        int icons[5] = {n_hp, n_def, n_atk, n_du, n_ss};
        SkillType types[5] = {SKILL_HP_UP, SKILL_DEF_UP, SKILL_ATK_UP, SKILL_DUAL_WIELD, SKILL_STAFF_SPEED};
        int ix = iconX;
        for (int k = 0; k < 5; k++) {
            for (int rep = 0; rep < icons[k]; rep++) {
                if (ix + iconW > LCD_WIDTH - 80) break;  /* 留80px给DEF */
                const uint16_t* ic = SKILL_ICONS[types[k]];
                for (int py = 0; py < 5; py++)
                    for (int px = 0; px < 5; px++) {
                        uint16_t col = ic[py*5+px];
                        if (col != 0x0000)
                            lcd_fill(ix+px*iconScale, iconY+py*iconScale,
                                     ix+px*iconScale+iconScale-1, iconY+py*iconScale+iconScale-1, col);
                    }
                ix += iconW + iconGap;
            }
        }
        x = ix + 4;  /* DEF从技能图标后面开始 */
    }

    /* DEF */
    for (i = 0; i < get_max_def(); i++) {
        uint16_t c = (i < g_def) ? GREEN : 0x4208;
        lcd_fill(x, 1, x + bw - 1, 1 + bh - 1, c);
        x += bw + gap;
    }

    /* 下半行左: 能量条（贴左边缘，15格，按比例减少）*/
    {
        int blocks = 15;
        int perBlock = ENERGY_MAX / blocks;
        x = 2;
        for (i = 0; i < blocks; i++) {
            uint16_t c = (g_energy >= (i + 1) * perBlock) ? YELLOW : 0x4208;
            lcd_fill(x, 12, x + bw - 1, 12 + bh - 1, c);
            x += bw + gap;
        }
    }
    
    /* 下半行: 当前武器图标（能量条右侧）*/
    {
        int wx = 2 + 15 * (bw + gap) + 5;  /* 能量条右侧留5像素间隔 */
        int wy = 11;
        if (g_currentWeapon == WEAPON_PISTOL) {
            /* 手枪图标：小矩形+枪管 */
            lcd_fill(wx, wy + 2, wx + 8, wy + 5, WHITE);       /* 枪身（白色）*/
            lcd_fill(wx + 8, wy + 3, wx + 12, wy + 4, 0x7BEF); /* 枪管（灰色）*/
        } else if (g_currentWeapon == WEAPON_SHOTGUN) {
            /* 霰弹枪图标：长矩形+粗枪管 */
            lcd_fill(wx, wy + 1, wx + 14, wy + 5, 0xF800);     /* 枪身（橙色）*/
            lcd_fill(wx + 14, wy + 2, wx + 18, wy + 4, 0x7BEF);/* 枪管（灰色）*/
        } else if (g_currentWeapon == WEAPON_SMG) {
            /* 冲锋枪图标：绿色短矩形+三个发射线条 */
            lcd_fill(wx, wy + 1, wx + 10, wy + 5, 0x07E0);      /* 枪身（绿色）*/
            lcd_fill(wx + 10, wy + 2, wx + 13, wy + 4, 0x7BEF);/* 枪管（灰色）*/
            lcd_fill(wx + 3, wy + 6, wx + 4, wy + 7, 0x07E0);  /* 弹匣（绿色）*/
        } else if (g_currentWeapon == WEAPON_LASER) {
            /* 激光炮图标：青色圆盘（发射器）+光束 */
            if (g_laserActive == 2) {
                /* 充能中：红色闪烁 */
                lcd_draw_circle(wx + 4, wy + 3, 3, RED);
            } else {
                lcd_draw_circle(wx + 4, wy + 3, 3, CYAN);           /* 发射器（青色圆）*/
            }
            lcd_fill(wx + 7, wy + 2, wx + 17, wy + 4, CYAN);   /* 光束（青色）*/
            lcd_fill(wx + 7, wy + 2, wx + 17, wy + 3, WHITE);  /* 光束高光（白色）*/
        } else if (g_currentWeapon == WEAPON_STAFF) {
            /* 法杖图标：紫色圆球+棒身 */
            lcd_fill(wx + 1, wy + 5, wx + 3, wy + 9, 0xA000);  /* 棒身（紫色）*/
            lcd_fill(wx + 1, wy + 1, wx + 3, wy + 3, 0xA000);  /* 棒身（紫色）*/
            lcd_draw_circle(wx + 2, wy + 2, 2, 0xA000);         /* 棒顶（紫色）*/
            lcd_draw_circle(wx + 2, wy + 2, 2, 0xF81F);         /* 能量球（粉紫色）*/
        } else if (g_currentWeapon == WEAPON_ROCKET) {
            /* 火箭炮图标：红色粗管+弹头 */
            lcd_fill(wx, wy + 2, wx + 10, wy + 5, RED);         /* 管身（红色）*/
            lcd_fill(wx + 10, wy + 1, wx + 14, wy + 6, 0xFC00);/* 弹头（橙色）*/
            lcd_fill(wx + 13, wy + 2, wx + 16, wy + 5, YELLOW); /* 尖端（黄色）*/
            lcd_fill(wx + 3, wy + 6, wx + 5, wy + 8, 0x9000);  /* 握把（深红）*/
        }
    }

    /* 右侧: 彩蛋机信息/商店信息(左) + 金币(中) + 数字(右) */
    {
        int coinCX = LCD_WIDTH - 55;
        int showPrice = 0;
        char priceStr[8] = {0};
        
        /* 彩蛋机信息：显示在金币左边 */
        if (g_gachaponX >= 0 && !g_gachaponUsed && !g_gachaponAnimActive) {
            int px = (int)(playerX + 0.5f);
            int py = (int)(playerY + 0.5f);
            /* 检查玩家是否站在彩蛋机上 */
            if (px == g_gachaponX && py == g_gachaponY) {
                int cost = get_gachapon_cost();
                sprintf(priceStr, "-%d", cost);
                showPrice = 1;
            }
        }
        
        /* 商店商品信息：当不在彩蛋机上时，检查是否站在商品上 */
        if (!showPrice) {
            int px = (int)(playerX + 0.5f);
            int py = (int)(playerY + 0.5f);
            for (int i = 0; i < 5; i++) {
                if (g_itemPos[i][0] == px && g_itemPos[i][1] == py) {
                    sprintf(priceStr, "-%d", g_itemCost[i]);
                    showPrice = 1;
                    break;
                }
            }
        }
        
        /* 显示价格（如果有）*/
        if (showPrice) {
            lcd_fill(coinCX - 35, 2, coinCX - 5, 14, BLACK);
            lcd_show_string(coinCX - 35, 2, 30, 12, 12, priceStr, RED);
        }
        
        /* 纯黄色圆盘，半径8，圆心y=9保证完全在屏幕内(y=1~17) */
        lcd_draw_circle(coinCX, 9, 8, YELLOW);
        lcd_draw_circle(coinCX, 9, 7, YELLOW);
        /* 数字在金币右侧，白字黑底 */
        draw_num_at(coinCX + 12, 2, g_gold, WHITE, BLACK);
        
        /* 彩蛋机动画：购买时显示在金币左边 */
        if (g_gachaponAnimActive) {
            uint32_t elapsed = HAL_GetTick() - g_gachaponAnimStart;
            int progress = (elapsed * 100) / 1000;  /* 0-100 */
            if (progress > 100) progress = 100;
            
            /* 在金币左边显示进度条动画 */
            int ax = coinCX - 40;  /* 金币左边 */
            int ay = 2;
            /* 清黑色背景 */
            lcd_fill(ax, ay, ax + 35, ay + 12, BLACK);
            /* 进度条边框 */
            lcd_draw_rectangle(ax, ay, ax + 35, ay + 12, YELLOW);
            /* 进度填充 */
            int pw = (31 * progress) / 100;
            lcd_fill(ax + 2, ay + 2, ax + 2 + pw, ay + 10, YELLOW);
        }
    }
}

/* ==================== 更新摇杆 ==================== */
static void update_joystick(void) {
    tp_dev.scan(0);
    if (tp_dev.sta & TP_PRES_DOWN) {
        uint16_t tx = tp_dev.x[0];
        uint16_t ty = tp_dev.y[0];
        int dx = tx - joystickCenterX;
        int dy = ty - joystickCenterY;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist <= JOYSTICK_RADIUS) {
            knobX = tx;
            knobY = ty;
            joystickActive = 1;
            if (dist > 1.0f) {
                joystickDirX = dx / dist;
                joystickDirY = dy / dist;
            } else {
                joystickDirX = 0.0f;
                joystickDirY = 0.0f;
            }
        } else {
            float angle = atan2f(dy, dx);
            knobX = joystickCenterX + (int)(JOYSTICK_RADIUS * cosf(angle));
            knobY = joystickCenterY + (int)(JOYSTICK_RADIUS * sinf(angle));
            joystickActive = 1;
            joystickDirX = cosf(angle);
            joystickDirY = sinf(angle);
        }
    } else {
        knobX = joystickCenterX;
        knobY = joystickCenterY;
        joystickActive = 0;
        joystickDirX = 0.0f;
        joystickDirY = 0.0f;
    }
}

/* ==================== 子弹系统 ==================== */
#define MAX_BULLETS 80
#define BULLET_SPEED 8.0f

typedef struct {
    float x, y;       /* 当前世界坐标 */
    float oldX, oldY; /* 上一帧世界坐标（用于逐格碰撞检测）*/
    float dx, dy;     /* 方向向量（单位向量）*/
    int active;       /* 是否存活 */
    uint8_t type;     /* 子弹类型：0=普通，1=法杖大弹 */
} Bullet;

static Bullet g_bullets[MAX_BULLETS];

/* 发射子弹（朝指定方向）*/
static void fire_bullet(float dirX, float dirY) {
    /* 找一个空闲子弹槽 */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].oldX = playerX;
            g_bullets[i].oldY = playerY;
            g_bullets[i].dx = dirX;
            g_bullets[i].dy = dirY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 0;  /* 普通子弹 */
            break;
        }
    }
}

/* 发射霰弹枪（三颗子弹，中间对准目标，两侧各偏移15度）*/
static void fire_shotgun(float dirX, float dirY) {
    /* 找一个空闲子弹槽 */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].dx = dirX;
            g_bullets[i].dy = dirY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 0;
            break;
        }
    }
    /* 左侧子弹（逆时针旋转15度）*/
    float angle = 15.0f * 3.14159f / 180.0f;
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    float leftX = dirX * cosA - dirY * sinA;
    float leftY = dirX * sinA + dirY * cosA;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].oldX = playerX;
            g_bullets[i].oldY = playerY;
            g_bullets[i].dx = leftX;
            g_bullets[i].dy = leftY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 0;
            break;
        }
    }
    /* 右侧子弹（顺时针旋转15度）*/
    float rightX = dirX * cosA + dirY * sinA;
    float rightY = -dirX * sinA + dirY * cosA;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].oldX = playerX;
            g_bullets[i].oldY = playerY;
            g_bullets[i].dx = rightX;
            g_bullets[i].dy = rightY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 0;
            break;
        }
    }
}

/* 发射冲锋枪（单发，用于连发模式）*/
static void fire_smg(float dirX, float dirY) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].oldX = playerX;
            g_bullets[i].oldY = playerY;
            g_bullets[i].dx = dirX;
            g_bullets[i].dy = dirY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 0;
            break;
        }
    }
}

/* 发射法杖（四面八方一圈大慢弹）*/
static void fire_staff(float dirX, float dirY) {
    (void)dirX;  /* 未使用方向参数，四面八方 */
    int bulletCount = 8;
    float angleStep = 360.0f / bulletCount;
    for (int i = 0; i < bulletCount; i++) {
        float angle = angleStep * i * 3.14159f / 180.0f;
        float bx = cosf(angle);
        float by = sinf(angle);
        /* 找一个空闲子弹槽 */
        for (int j = 0; j < MAX_BULLETS; j++) {
            if (!g_bullets[j].active) {
                g_bullets[j].x = playerX;
                g_bullets[j].y = playerY;
                g_bullets[j].oldX = playerX;
                g_bullets[j].oldY = playerY;
                g_bullets[j].dx = bx;
                g_bullets[j].dy = by;
                g_bullets[j].active = 1;
                g_bullets[j].type = 1;  /* 法杖大弹 */
                break;
            }
        }
    }
}

/* 怪物死亡统一处理：金币奖励+能量+BOSS标记 */
static void on_monster_killed(Monster *m) {
    int reward = 3;
    if (m->type == MONSTER_TYPE_BOSS) { reward = 50; g_bossDefeated = 1; }
        else if (m->type == MONSTER_TYPE_BOSS2) { reward = 100; g_bossDefeated = 1; }
    else if (m->type == MONSTER_TYPE_D)  reward = 10;
    else if (m->type == MONSTER_TYPE_B)  reward = 5;
    else if (m->type == MONSTER_TYPE_C)  reward = 4;
    g_gold += reward;
    g_energy += ENERGY_PER_KILL;
    if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
}

/* 发射火箭弹（朝指定方向飞行，命中后3×3爆炸，不伤害玩家）*/
static void trigger_tnt_explosion(int tntIdx);  /* 前向声明：火箭爆炸可能连锁TNT */
static void fire_rocket(float dirX, float dirY) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) {
            g_bullets[i].x = playerX;
            g_bullets[i].y = playerY;
            g_bullets[i].oldX = playerX;
            g_bullets[i].oldY = playerY;
            g_bullets[i].dx = dirX;
            g_bullets[i].dy = dirY;
            g_bullets[i].active = 1;
            g_bullets[i].type = 2;  /* 2=火箭弹 */
            break;
        }
    }
}

/* 火箭弹爆炸：3×3范围，伤害怪物+箱子+TNT连锁，但不伤害玩家 */
static void trigger_rocket_explosion(float cx, float cy) {
    /* 创建爆炸动画（复用TNT动画系统）*/
    /* 找一个空闲TNT槽来播放动画 */
    for (int ti = 0; ti < MAX_TNTS; ti++) {
        if (g_tnts[ti].state == TNT_DESTROYED || g_tnts[ti].state == TNT_IDLE) {
            /* 用一个空闲TNT槽来播放火箭爆炸动画 */
            int icx = (int)(cx + 0.5f);
            int icy = (int)(cy + 0.5f);
            g_tnts[ti].x = icx;
            g_tnts[ti].y = icy;
            g_tnts[ti].state = TNT_EXPLODING;  /* 复用TNT爆炸动画 */
            g_tnts[ti].explodeStart = HAL_GetTick();
            g_tnts[ti].hp = 0;
            g_tntAnimActive = 1;
            break;
        }
    }

    /* 遍历爆炸范围 3×3 */
    int icx = (int)(cx + 0.5f);
    int icy = (int)(cy + 0.5f);
    for (int dy = -ROCKET_RADIUS; dy <= ROCKET_RADIUS; dy++) {
        for (int dx = -ROCKET_RADIUS; dx <= ROCKET_RADIUS; dx++) {
            int tx = icx + dx;
            int ty = icy + dy;
            if (tx < 0 || tx >= g_dungeon.width || ty < 0 || ty >= g_dungeon.height)
                continue;

            uint8_t tile = g_dungeon.map[ty][tx];
            if (tile == TILE_WALL) continue;  /* 爆炸穿不透墙壁 */

            /* ===== 怪物 ===== */
            for (int mi = 0; mi < g_monsterCount; mi++) {
                Monster *m = &g_monsters[mi];
                if (!m->alive) continue;
                int mx = (int)(m->x + 0.5f);
                int my = (int)(m->y + 0.5f);
                if (mx == tx && my == ty) {
                    m->hp -= ROCKET_EXPLODE_DMG;
                    if (m->hp <= 0) {
                        m->alive = 0;
                        on_monster_killed(m);
                    }
                }
            }

            /* ===== 箱子 ===== */
            for (int ci = 0; ci < g_crateCount; ci++) {
                Crate *cr = &g_crates[ci];
                if (!cr->alive) continue;
                if (cr->x == tx && cr->y == ty) {
                    cr->hp -= ROCKET_EXPLODE_DMG;
                    if (cr->hp <= 0) {
                        cr->alive = 0;
                        g_dungeon.map[ty][tx] = TILE_CRATE_BROKEN;
                        if (dungeon_rand(&g_dungeon, 100) < CRATE_DROP_RATE) {
                            g_dungeon.map[ty][tx] = (dungeon_rand(&g_dungeon, 100) < 50) ?
                                TILE_ENERGY_POTION : TILE_POTION;
                        }
                        g_energy += 3 + dungeon_rand(&g_dungeon, 6);
                        if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                    }
                }
            }

            /* ===== 地刺收回 ===== */
            if (tile == TILE_SPIKE_UP || tile == TILE_SPIKE_DOWN) {
                g_dungeon.map[ty][tx] = TILE_SPIKE_DOWN;
            }

            /* ===== TNT连锁引爆 ===== */
            if (tile == TILE_TNT) {
                for (int ti = 0; ti < MAX_TNTS; ti++) {
                    TNT *ot = &g_tnts[ti];
                    if (ot->state == TNT_IDLE && ot->x == tx && ot->y == ty) {
                        trigger_tnt_explosion(ti);  /* TNT爆炸会伤害玩家 */
                    }
                }
            }
            /* 注意：火箭弹爆炸不伤害玩家！这是核心区别 */
        }
    }
}

/* 激光每0.5秒对光束上的怪物造成1点伤害（仅在激活态=1时）*/
static void update_laser(uint32_t now) {
    if (g_laserActive != 1) return;  /* 只有激活态才造成伤害 */
    /* 每0.5秒造成一次伤害 */
    if (now - g_laserStartTick >= 500) {
        g_laserStartTick = now;  /* 重置伤害计时器 */
        /* 遍历所有怪物，检查是否在激光光束上 */
        float ldx = g_laserEndX - g_laserStartX;
        float ldy = g_laserEndY - g_laserStartY;
        float len2 = ldx*ldx + ldy*ldy;
        if (len2 < 0.001f) return;
        float len = sqrtf(len2);
        float ux = ldx / len;  /* 光束单位方向 */
        float uy = ldy / len;
        for (int mi = 0; mi < g_monsterCount; mi++) {
            if (!g_monsters[mi].alive) continue;
            float mx = g_monsters[mi].x;
            float my = g_monsters[mi].y;
            /* 点到线段的投影参数t */
            float t = ((mx - g_laserStartX) * ux + (my - g_laserStartY) * uy) / len;
            if (t < 0.0f || t > 1.0f) continue;  /* 不在光束范围内 */
            /* 光束上离怪物最近的点 */
            float cx = g_laserStartX + t * ldx;
            float cy = g_laserStartY + t * ldy;
            float dist = sqrtf((mx-cx)*(mx-cx) + (my-cy)*(my-cy));
            if (dist < 1.5f) {  /* 距离小于1.5格 */
                /* 内联怪物扣血逻辑（同check_bullet_hit_monsters）*/
                g_monsters[mi].hp -= get_laser_dmg();  /* 激光伤害+技能加成 */
                if (g_monsters[mi].hp <= 0) {
                    g_monsters[mi].alive = 0;
                    on_monster_killed(&g_monsters[mi]);
                }
            }
        }
    }
}

/* 更新所有子弹位置 */
static void trigger_tnt_explosion(int tntIdx);  /* 前向声明 */
static void update_bullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) continue;
        
        /* 移动：法杖子弹使用更慢的速度 */
        float speed = (g_bullets[i].type == 1) ? STAFF_BULLET_SPEED :
                      (g_bullets[i].type == 2) ? ROCKET_SPEED : BULLET_SPEED;
        g_bullets[i].x += g_bullets[i].dx * speed * dt;
        g_bullets[i].y += g_bullets[i].dy * speed * dt;

        /* 用世界坐标直接检测子弹是否在TNT碰撞范围内（O(3)，最可靠）*/
        /* TNT占据的格子范围：[t->x, t->x+1) × [t->y, t->y+1)，转成世界像素坐标 */
        {
            int hitTNT = 0;
            for (int ti = 0; ti < MAX_TNTS; ti++) {
                TNT *t = &g_tnts[ti];
                if (t->state != TNT_IDLE) continue;
                float tx_world = (float)(t->x + 0) * CELL_SIZE;  /* TNT格子左边界（像素）*/
                float ty_world = (float)(t->y + 0) * CELL_SIZE;  /* TNT格子上边界（像素）*/
                float tx_end   = (float)(t->x + 1) * CELL_SIZE;  /* TNT格子右边界（像素）*/
                float ty_end   = (float)(t->y + 1) * CELL_SIZE;  /* TNT格子下边界（像素）*/
                float bx = g_bullets[i].x * CELL_SIZE;  /* 子弹世界坐标（像素）*/
                float by = g_bullets[i].y * CELL_SIZE;
                /* 子弹是否进入TNT格子？只要中心进入就算（子弹很小，只有点击判定）*/
                if (bx > tx_world && bx < tx_end && by > ty_world && by < ty_end) {
                    if (g_bullets[i].type == 2) {
                        /* 火箭弹命中TNT：触发火箭爆炸（会连锁引爆TNT）*/
                        trigger_rocket_explosion(g_bullets[i].x, g_bullets[i].y);
                    } else {
                        t->hp -= 1;
                        if (t->hp <= 0) trigger_tnt_explosion(ti);
                    }
                    hitTNT = 1;
                    break;
                }
            }
            if (hitTNT) {
                g_bullets[i].active = 0;
                continue;
            }
        }
        
        /* 更新旧位置 */
        g_bullets[i].oldX = g_bullets[i].x;
        g_bullets[i].oldY = g_bullets[i].y;
        
        /* 出界或撞墙：普通子弹销毁，火箭弹爆炸 */
        int bx = (int)(g_bullets[i].x + 0.5f);
        int by = (int)(g_bullets[i].y + 0.5f);
        if (bx < 0 || bx >= g_dungeon.width || 
            by < 0 || by >= g_dungeon.height ||
            !dungeon_is_walkable(&g_dungeon, bx, by)) {
            if (g_bullets[i].type == 2) {
                /* 火箭弹撞墙/出界：在碰撞点爆炸 */
                float ex = g_bullets[i].x - g_bullets[i].dx * speed * dt;
                float ey = g_bullets[i].y - g_bullets[i].dy * speed * dt;
                trigger_rocket_explosion(ex, ey);
            }
            g_bullets[i].active = 0;
        }
    }
}

/* 绘制所有子弹 */
static void draw_bullets(void) {
    int screenCenterX = LCD_WIDTH / 2;
    int screenCenterY = LCD_HEIGHT / 2 - 40;  /* 和render_dungeon一致 */
    
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) continue;
        
        /* 子弹世界坐标 → 屏幕坐标 */
        int sx = screenCenterX + (int)((g_bullets[i].x - playerX) * CELL_SIZE);
        int sy = screenCenterY + (int)((g_bullets[i].y - playerY) * CELL_SIZE);
        
        /* 屏幕范围检查 */
        if (sx < 0 || sx >= LCD_WIDTH || sy < 0 || sy >= LCD_HEIGHT) continue;
        
        /* 法杖子弹：更大的紫色圆球 */
        if (g_bullets[i].type == 1) {
            fill_circle_fast(sx, sy, STAFF_BULLET_RADIUS, 0xA000); /* 紫色 */
        } else if (g_bullets[i].type == 2) {
            /* 火箭弹：红色大圆 + 橙色尾焰 */
            fill_circle_fast(sx, sy, 4, RED);             /* 弹头（红色）*/
            fill_circle_fast(sx, sy, 2, 0xFC00);          /* 弹头高光（橙色）*/
            /* 尾焰：沿反方向画2个小圆 */
            int tailX = sx - (int)(g_bullets[i].dx * 4);
            int tailY = sy - (int)(g_bullets[i].dy * 4);
            fill_circle_fast(tailX, tailY, 3, 0xFC00);    /* 尾焰（橙色）*/
            int tailX2 = sx - (int)(g_bullets[i].dx * 7);
            int tailY2 = sy - (int)(g_bullets[i].dy * 7);
            fill_circle_fast(tailX2, tailY2, 2, YELLOW);  /* 尾焰尖端（黄色）*/
        } else {
            /* 普通子弹：小黄点 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, YELLOW);
        }
    }
}

/* ==================== 绘制激光光束 ==================== */
static void draw_laser(void) {
    if (g_laserActive != 1) return;  /* 只有激活态(1)才绘制，充能态(2)不绘制 */
    
    int screenCenterX = LCD_WIDTH / 2;
    int screenCenterY = LCD_HEIGHT / 2 - 40;
    
    /* 世界坐标 → 屏幕坐标 */
    int sx1 = screenCenterX + (int)((g_laserStartX - playerX) * CELL_SIZE);
    int sy1 = screenCenterY + (int)((g_laserStartY - playerY) * CELL_SIZE);
    int sx2 = screenCenterX + (int)((g_laserEndX - playerX) * CELL_SIZE);
    int sy2 = screenCenterY + (int)((g_laserEndY - playerY) * CELL_SIZE);
    
    /* 画外发光（粗线，青色，向两侧各偏移1像素形成厚度）*/
    draw_line_with_map(sx1 - 1, sy1 - 1, sx2 - 1, sy2 - 1, CYAN);
    draw_line_with_map(sx1,     sy1 - 1, sx2,     sy2 - 1, CYAN);
    draw_line_with_map(sx1 + 1, sy1 - 1, sx2 + 1, sy2 - 1, CYAN);
    draw_line_with_map(sx1 - 1, sy1,     sx2 - 1, sy2,     CYAN);
    draw_line_with_map(sx1 + 1, sy1,     sx2 + 1, sy2,     CYAN);
    draw_line_with_map(sx1 - 1, sy1 + 1, sx2 - 1, sy2 + 1, CYAN);
    draw_line_with_map(sx1,     sy1 + 1, sx2,     sy2 + 1, CYAN);
    draw_line_with_map(sx1 + 1, sy1 + 1, sx2 + 1, sy2 + 1, CYAN);
    /* 画内核（细线，白色高亮）*/
    draw_line_with_map(sx1, sy1, sx2, sy2, WHITE);
}

/* ==================== 玩家所在房间检测 ==================== */
static int get_player_room(void) {
    int px = (int)(playerX + 0.5f);
    int py = (int)(playerY + 0.5f);
    for (int i = 0; i < g_dungeon.room_count; i++) {
        Room *r = &g_dungeon.rooms[i];
        if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h)
            return i;
    }
    return -1;  /* 走廊里 */
}

/* ==================== 怪物AI更新 ==================== */
static void trigger_tnt_explosion(int tntIdx);  /* 前向声明：怪物可能引爆TNT */
static void update_monsters(float dt, uint32_t now) {
    int px = (int)(playerX + 0.5f);
    int py = (int)(playerY + 0.5f);

    for (int i = 0; i < g_monsterCount; i++) {
        Monster *m = &g_monsters[i];
        if (!m->alive) continue;

        /* 检查玩家是否在本怪物房间内 */
        Room *r = &g_dungeon.rooms[m->room_idx];
        int playerInRoom = (px >= r->x && px < r->x + r->w &&
                            py >= r->y && py < r->y + r->h);

        if (!playerInRoom) continue;  /* 玩家不在房间，怪物静止 */

        /* 朝向玩家 */
        float ddx = playerX - m->x;
        float ddy = playerY - m->y;
        float dist = sqrtf(ddx*ddx + ddy*ddy);
        if (dist > 0.1f) {
            if (ddx*ddx > ddy*ddy) {
                m->dir = (ddx > 0) ? 3 : 2;
            } else {
                m->dir = (ddy > 0) ? 0 : 1;
            }
        }

        /* 移动（朝玩家方向，但只在房间内、不穿墙）*/
        if (dist > 1.5f) {  /* 距离>1.5格时才移动 */
            float spd = MONSTER_SPEED * dt;
            float newX, newY;

            /* X轴移动（优先沿X轴方向移动） */
            if (ddx*ddx > ddy*ddy) {
                newX = m->x + (ddx > 0 ? spd : -spd);
                int ni = (int)(newX + 0.5f);
                /* 注意：检测用移动后的mj（不能只检测旧位置，否则踩 TNT 检测不到）*/
                int curMj = (int)(m->y + 0.5f);  // 移动前先记住当前Y
                if (ni > m->rx1 && ni < m->rx2 && monster_walkable(ni, curMj)) {
                    m->x = newX;
                }
                /* X轴移动完成后：检查是否踏上TNT（用移动后的X和当前Y）*/
                {
                    int mx = (int)(m->x + 0.5f);
                    int my = (int)(m->y + 0.5f);
                    for (int ti = 0; ti < MAX_TNTS; ti++) {
                        TNT *t = &g_tnts[ti];
                        if (t->state == TNT_IDLE && t->x == mx && t->y == my) {
                            t->hp -= 1;
                            if (t->hp <= 0) {
                                trigger_tnt_explosion(ti);
                            }
                            break;
                        }
                    }
                }
                /* Y轴移动（次优先）*/
                newY = m->y + (ddy > 0 ? spd : -spd);
                int curMi = (int)(m->x + 0.5f);  // X轴移动后重新取整
                int nj = (int)(newY + 0.5f);
                if (nj > m->ry1 && nj < m->ry2 && monster_walkable(curMi, nj)) {
                    m->y = newY;
                }
                /* Y轴移动完成后：检查是否踏上TNT（用当前X和移动后的Y）*/
                {
                    int mx = (int)(m->x + 0.5f);
                    int my = (int)(m->y + 0.5f);
                    for (int ti = 0; ti < MAX_TNTS; ti++) {
                        TNT *t = &g_tnts[ti];
                        if (t->state == TNT_IDLE && t->x == mx && t->y == my) {
                            t->hp -= 1;
                            if (t->hp <= 0) {
                                trigger_tnt_explosion(ti);
                            }
                            break;
                        }
                    }
                }
            } else {
                /* Y轴优先 */
                newY = m->y + (ddy > 0 ? spd : -spd);
                int curMi = (int)(m->x + 0.5f);
                int nj = (int)(newY + 0.5f);
                if (nj > m->ry1 && nj < m->ry2 && monster_walkable(curMi, nj)) {
                    m->y = newY;
                }
                /* Y轴移动完成后：检查TNT */
                {
                    int mx = (int)(m->x + 0.5f);
                    int my = (int)(m->y + 0.5f);
                    for (int ti = 0; ti < MAX_TNTS; ti++) {
                        TNT *t = &g_tnts[ti];
                        if (t->state == TNT_IDLE && t->x == mx && t->y == my) {
                            t->hp -= 1;
                            if (t->hp <= 0) {
                                trigger_tnt_explosion(ti);
                            }
                            break;
                        }
                    }
                }
                /* X轴次优先 */
                newX = m->x + (ddx > 0 ? spd : -spd);
                int curMj = (int)(m->y + 0.5f);
                int ni = (int)(newX + 0.5f);
                if (ni > m->rx1 && ni < m->rx2 && monster_walkable(ni, curMj)) {
                    m->x = newX;
                }
                /* X轴移动完成后：检查TNT */
                {
                    int mx = (int)(m->x + 0.5f);
                    int my = (int)(m->y + 0.5f);
                    for (int ti = 0; ti < MAX_TNTS; ti++) {
                        TNT *t = &g_tnts[ti];
                        if (t->state == TNT_IDLE && t->x == mx && t->y == my) {
                            t->hp -= 1;
                            if (t->hp <= 0) {
                                trigger_tnt_explosion(ti);
                            }
                            break;
                        }
                    }
                }
            }
        }

        /* 跑动动画 */
        if (dist > 1.5f) {
            if (now - m->lastAnimTick >= 150) {
                m->lastAnimTick = now;
                m->animFrame = (m->animFrame == 1) ? 2 : 1;
            }
        } else {
            m->animFrame = 0;
        }

        /* 攻击行为：根据怪物类型区分 */
        /* A类：近战攻击（和原来一样） */
        if (dist <= 1.5f && now - m->lastMelee >= MELEE_INTERVAL) {
            m->lastMelee = now;
            player_take_damage(MONSTER_A_DMG);
        }

        /* B类：巨型远程子弹（单发，伤害高，速度快，间隔长） */
        if (m->type == MONSTER_TYPE_B && now - m->lastRanged >= RANGED_INTERVAL_B) {
            m->lastRanged = now;
            if (dist > 0.1f) {
                /* 寻找空闲子弹槽 */
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        float ndx = ddx / dist;
                        float ndy = ddy / dist;
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = ndx;
                        g_mbullets[bi].dy = ndy;
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = MONSTER_TYPE_B;  /* 标记为B类子弹 */
                        break;
                    }
                }
            }
        }

        /* C类：大量远程子弹（5发散弹，伤害中等，速度稍慢，间隔短） */
        if (m->type == MONSTER_TYPE_C && now - m->lastRanged >= RANGED_INTERVAL_C) {
            m->lastRanged = now;
            if (dist > 0.1f) {
                float ndx = ddx / dist;
                float ndy = ddy / dist;
                /* 5发扇形子弹，更密集 */
                for (int b = 0; b < 5; b++) {
                    for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                        if (!g_mbullets[bi].active) {
                            float angle = atan2f(ndy, ndx) + (b - 2) * 0.2f;
                            g_mbullets[bi].x = m->x;
                            g_mbullets[bi].y = m->y;
                            g_mbullets[bi].dx = cosf(angle);
                            g_mbullets[bi].dy = sinf(angle);
                            g_mbullets[bi].active = 1;
                            g_mbullets[bi].type = MONSTER_TYPE_C;  /* 标记为C类子弹 */
                            break;
                        }
                    }
                }
            }
        }

        /* D类：宝箱守卫，环形子弹（向四面八方发出8颗子弹） */
        if (m->type == MONSTER_TYPE_D && now - m->lastRanged >= RANGED_INTERVAL_D) {
            m->lastRanged = now;
            /* 环形8颗子弹 */
            for (int b = 0; b < 8; b++) {
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        float angle = b * (6.28318f / 8.0f);  /* 每45度一颗 */
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = cosf(angle);
                        g_mbullets[bi].dy = sinf(angle);
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = MONSTER_TYPE_D;  /* 标记为D类子弹 */
                        break;
                    }
                }
            }
        }

        /* BOSS：三种攻击方式 */
        if (m->type == MONSTER_TYPE_BOSS) {
            /* 第一种：巨大子弹，比B类更大，向四面八方发出10颗 */
            if (now - m->lastRanged >= BOSS_INTERVAL1) {
                m->lastRanged = now;
                for (int b = 0; b < 10; b++) {
                    for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                        if (!g_mbullets[bi].active) {
                            float angle = b * (6.28318f / 10.0f);  /* 每36度一颗，更密集 */
                            g_mbullets[bi].x = m->x;
                            g_mbullets[bi].y = m->y;
                            g_mbullets[bi].dx = cosf(angle);
                            g_mbullets[bi].dy = sinf(angle);
                            g_mbullets[bi].active = 1;
                            g_mbullets[bi].type = 6;  /* BOSS巨大子弹 */
                            g_mbullets[bi].splitTick = 0;  /* 不分裂 */
                            break;
                        }
                    }
                }
            }

            /* 第二种：快速小子弹，速度快，数量多 */
            if (now - m->lastRanged2 >= BOSS_INTERVAL2) {
                m->lastRanged2 = now;
                if (dist > 0.1f) {
                    float ndx = ddx / dist;
                    float ndy = ddy / dist;
                    /* 5发扇形快速子弹 */
                    for (int b = 0; b < 5; b++) {
                        for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                            if (!g_mbullets[bi].active) {
                                float angle = atan2f(ndy, ndx) + (b - 2) * 0.25f;
                                g_mbullets[bi].x = m->x;
                                g_mbullets[bi].y = m->y;
                                g_mbullets[bi].dx = cosf(angle);
                                g_mbullets[bi].dy = sinf(angle);
                                g_mbullets[bi].active = 1;
                                g_mbullets[bi].type = 7;  /* BOSS快速小子弹 */
                                g_mbullets[bi].splitTick = 0;
                                break;
                            }
                        }
                    }
                }
            }

            /* 第三种：最强子弹，速度慢，伤害5，移动中会向四面八方发出小子弹（像飞行的C类怪） */
            if (now - m->lastRanged3 >= BOSS_INTERVAL3) {
                m->lastRanged3 = now;
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        /* 射向玩家方向 */
                        float angle = atan2f(ddy, ddx);
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = cosf(angle);
                        g_mbullets[bi].dy = sinf(angle);
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = 8;  /* BOSS最强子弹，会分裂 */
                        g_mbullets[bi].splitTick = 0;
                        break;
                    }
                }
            }
        }

        /* BOSS2：5种攻击方式（前3种同BOSS，后2种是BOSS2专属）*/
        if (m->type == MONSTER_TYPE_BOSS2) {
            /* BOSS2第一招：环形巨大子弹（和BOSS第一招一样）*/
            if (now - m->lastRanged >= BOSS_INTERVAL1) {
                m->lastRanged = now;
                for (int b = 0; b < 10; b++) {
                    for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                        if (!g_mbullets[bi].active) {
                            float angle = b * (6.28318f / 10.0f);
                            g_mbullets[bi].x = m->x;
                            g_mbullets[bi].y = m->y;
                            g_mbullets[bi].dx = cosf(angle);
                            g_mbullets[bi].dy = sinf(angle);
                            g_mbullets[bi].active = 1;
                            g_mbullets[bi].type = 6;  /* BOSS2环形巨大子弹 */
                            g_mbullets[bi].splitTick = 0;
                            break;
                        }
                    }
                }
            }

            /* BOSS2第二招：快速扇形小子弹（和BOSS第二招一样）*/
            if (now - m->lastRanged2 >= BOSS_INTERVAL2) {
                m->lastRanged2 = now;
                if (dist > 0.1f) {
                    float ndx = ddx / dist;
                    float ndy = ddy / dist;
                    for (int b = 0; b < 5; b++) {
                        for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                            if (!g_mbullets[bi].active) {
                                float angle = atan2f(ndy, ndx) + (b - 2) * 0.25f;
                                g_mbullets[bi].x = m->x;
                                g_mbullets[bi].y = m->y;
                                g_mbullets[bi].dx = cosf(angle);
                                g_mbullets[bi].dy = sinf(angle);
                                g_mbullets[bi].active = 1;
                                g_mbullets[bi].type = 7;  /* BOSS2快速小子弹 */
                                g_mbullets[bi].splitTick = 0;
                                break;
                            }
                        }
                    }
                }
            }

            /* BOSS2第三招：最强子弹（和BOSS第三招一样）*/
            if (now - m->lastRanged3 >= BOSS_INTERVAL3) {
                m->lastRanged3 = now;
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        float angle = atan2f(ddy, ddx);
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = cosf(angle);
                        g_mbullets[bi].dy = sinf(angle);
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = 8;  /* BOSS2最强子弹 */
                        g_mbullets[bi].splitTick = 0;
                        break;
                    }
                }
            }

            /* BOSS2第四招：双向火箭弹（向前+向后同时发射，2.5秒间隔）
             * 使用type=9（火箭弹），触发与玩家火箭弹相同的爆炸效果 */
            if (now - m->lastRanged4 >= BOSS2_INTERVAL4) {
                m->lastRanged4 = now;
                /* 射向玩家方向（第一发）*/
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        float angle = atan2f(ddy, ddx);
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = cosf(angle);
                        g_mbullets[bi].dy = sinf(angle);
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = 9;  /* BOSS2火箭弹，命中爆炸 */
                        g_mbullets[bi].splitTick = 0;
                        break;
                    }
                }
                /* 射向玩家反方向（第二发，同时发射）*/
                for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                    if (!g_mbullets[bi].active) {
                        float angle = atan2f(-ddy, -ddx);  /* 反方向 */
                        g_mbullets[bi].x = m->x;
                        g_mbullets[bi].y = m->y;
                        g_mbullets[bi].dx = cosf(angle);
                        g_mbullets[bi].dy = sinf(angle);
                        g_mbullets[bi].active = 1;
                        g_mbullets[bi].type = 9;  /* BOSS2火箭弹 */
                        g_mbullets[bi].splitTick = 0;
                        break;
                    }
                }
            }

            /* BOSS2第五招：持续机枪扫射（持续1.5秒，4秒间隔）
             * 一旦开始，持续1.5秒内每100ms发射一发光弹 */
            if (now - m->lastRanged5 >= BOSS2_INTERVAL5) {
                m->lastRanged5 = now;
                m->mgStartTick = now;  /* 标记机枪扫射开始 */
            }
            /* 机枪扫射中（检查是否仍在1.5秒持续时间内）*/
            if (m->mgStartTick > 0 && now - m->mgStartTick < BOSS2_MG_DURATION) {
                /* 每BOSS2_MG_RATE毫秒发射一发光弹，射向玩家 */
                uint32_t mgInterval = BOSS2_MG_RATE;
                static uint32_t s_lastMgTick = 0;  /* 只在这个BOSS攻击中用，不持久化 */
                /* 用时间戳比较实现100ms间隔 */
                if (now - s_lastMgTick >= mgInterval) {
                    s_lastMgTick = now;
                    for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                        if (!g_mbullets[bi].active) {
                            float angle = atan2f(ddy, ddx);
                            g_mbullets[bi].x = m->x;
                            g_mbullets[bi].y = m->y;
                            g_mbullets[bi].dx = cosf(angle);
                            g_mbullets[bi].dy = sinf(angle);
                            g_mbullets[bi].active = 1;
                            g_mbullets[bi].type = 7;  /* 机枪光弹=类型7（快速小子弹视觉）*/
                            g_mbullets[bi].splitTick = 0;
                            break;
                        }
                    }
                }
            } else if (m->mgStartTick > 0 && now - m->mgStartTick >= BOSS2_MG_DURATION) {
                m->mgStartTick = 0;  /* 1.5秒结束，停止扫射 */
            }
        }
    }
}

/* ==================== 怪物子弹更新 ==================== */
static void update_mbullets(float dt) {
    for (int i = 0; i < MAX_MBULLETS; i++) {
        if (!g_mbullets[i].active) continue;

        /* 根据子弹类型使用不同速度 */
        float speed = MBULLET_SPEED;
        if (g_mbullets[i].type == MONSTER_TYPE_B) {
            speed = MBULLET_SPEED_B;  /* B类巨型子弹更快 */
        } else if (g_mbullets[i].type == MONSTER_TYPE_C) {
            speed = MBULLET_SPEED_C;  /* C类大量子弹稍慢 */
        } else if (g_mbullets[i].type == 6) {
            speed = MBULLET_SPEED_B;  /* BOSS巨大子弹速度和B类相同 */
        } else if (g_mbullets[i].type == 7) {
            speed = MBULLET_SPEED * 1.5f;  /* BOSS快速小子弹最快 */
        } else if (g_mbullets[i].type == 8) {
            speed = MBULLET_SPEED * 0.5f;  /* BOSS最强子弹最慢 */
        } else if (g_mbullets[i].type == 9) {
            speed = ROCKET_SPEED;  /* BOSS2火箭弹速度=玩家火箭弹 */
        }

        g_mbullets[i].x += g_mbullets[i].dx * speed * dt;
        g_mbullets[i].y += g_mbullets[i].dy * speed * dt;

        /* BOSS第三种最强子弹：每移动一段距离就向四面八方发出小子弹 */
        if (g_mbullets[i].type == 8) {
            g_mbullets[i].splitTick++;
            /* 每移动一定时间（相当于一定距离）分裂一次 */
            if (g_mbullets[i].splitTick > 30) {  /* 约0.3秒分裂一次 */
                g_mbullets[i].splitTick = 0;
                /* 向四面八方发出4颗C类子弹 */
                for (int b = 0; b < 4; b++) {
                    for (int bi = 0; bi < MAX_MBULLETS; bi++) {
                        if (!g_mbullets[bi].active) {
                            float angle = b * (6.28318f / 4.0f);
                            g_mbullets[bi].x = g_mbullets[i].x;
                            g_mbullets[bi].y = g_mbullets[i].y;
                            g_mbullets[bi].dx = cosf(angle);
                            g_mbullets[bi].dy = sinf(angle);
                            g_mbullets[bi].active = 1;
                            g_mbullets[bi].type = MONSTER_TYPE_C;  /* 分裂出C类子弹 */
                            g_mbullets[bi].splitTick = 0;
                            break;
                        }
                    }
                }
            }
        }

        int bx = (int)(g_mbullets[i].x + 0.5f);
        int by = (int)(g_mbullets[i].y + 0.5f);

        /* 撞墙/出界/撞箱子销毁（火箭弹撞墙则爆炸）*/
        if (bx < 0 || bx >= g_dungeon.width || by < 0 || by >= g_dungeon.height ||
            !dungeon_is_walkable(&g_dungeon, bx, by) ||
            g_dungeon.map[by][bx] == TILE_CRATE) {
            /* BOSS2火箭弹撞墙/箱子 → 爆炸 */
            if (g_mbullets[i].type == 9) {
                trigger_rocket_explosion(g_mbullets[i].x, g_mbullets[i].y);
            }
            g_mbullets[i].active = 0;
            continue;
        }

        /* 命中TNT → 扣TNT血，血量归零才炸 */
        if (g_dungeon.map[by][bx] == TILE_TNT) {
            for (int ti = 0; ti < MAX_TNTS; ti++) {
                TNT *t = &g_tnts[ti];
                if (t->state == TNT_IDLE && t->x == bx && t->y == by) {
                    t->hp -= 1;
                    if (t->hp <= 0) {
                        trigger_tnt_explosion(ti);
                    }
                    break;
                }
            }
            g_mbullets[i].active = 0;
            continue;
        }

        /* 命中玩家：根据子弹类型造成不同伤害 */
        if (bx == (int)(playerX + 0.5f) && by == (int)(playerY + 0.5f)) {
            /* 默认子弹伤害（A类近战伤害） */
            int damage = MONSTER_A_DMG;
            if (g_mbullets[i].type == MONSTER_TYPE_B) {
                damage = MONSTER_B_DMG;  /* B类巨型子弹伤害4 */
            } else if (g_mbullets[i].type == MONSTER_TYPE_C) {
                damage = MONSTER_C_DMG;  /* C类子弹伤害2 */
            } else if (g_mbullets[i].type == MONSTER_TYPE_D) {
                damage = MONSTER_D_DMG;  /* D类环形子弹伤害2 */
            } else if (g_mbullets[i].type == 6) {
                damage = MONSTER_BOSS_DMG1;  /* BOSS巨大子弹伤害3 */
            } else if (g_mbullets[i].type == 7) {
                damage = MONSTER_BOSS_DMG2;  /* BOSS快速小子弹伤害2 */
            } else if (g_mbullets[i].type == 8) {
                damage = MONSTER_BOSS_DMG3;  /* BOSS最强子弹伤害5 */
            } else if (g_mbullets[i].type == 9) {
                damage = MONSTER_BOSS2_DMG4;  /* BOSS2火箭弹伤害5（=爆炸伤害） */
            }
            player_take_damage(damage);
            g_mbullets[i].active = 0;
        }
    }
}

/* ==================== 玩家子弹命中怪物检测 ==================== */
/* ==================== TNT 引爆核心逻辑 ==================== */
/*
 * 触发指定TNT的爆炸：
 * 1. 立即标记TNT为爆炸状态，开始动画
 * 2. 对周围爆炸范围内的怪物造成伤害
 * 3. 对玩家造成伤害
 * 4. 破坏范围内的箱子
 * 5. 破坏范围内的其他TNT（连锁爆炸）
 * 6. 如果范围内有地刺，将地刺收回（无伤害）
 */
static void trigger_tnt_explosion(int tntIdx) {
    if (tntIdx < 0 || tntIdx >= MAX_TNTS) return;
    TNT *t = &g_tnts[tntIdx];
    if (t->state != TNT_IDLE) return;  /* 已经在爆炸或销毁 */

    /* 触发爆炸 */
    t->state = TNT_EXPLODING;
    t->explodeStart = HAL_GetTick();
    g_tntAnimActive = 1;

    int cx = t->x;
    int cy = t->y;

    /* 遍历爆炸范围 3×3 */
    for (int dy = -TNT_RADIUS; dy <= TNT_RADIUS; dy++) {
        for (int dx = -TNT_RADIUS; dx <= TNT_RADIUS; dx++) {
            int tx = cx + dx;
            int ty = cy + dy;
            if (tx < 0 || tx >= g_dungeon.width || ty < 0 || ty >= g_dungeon.height)
                continue;

            uint8_t tile = g_dungeon.map[ty][tx];

            /* 墙壁：跳过（爆炸穿不透墙壁）*/
            if (tile == TILE_WALL) continue;

            /* ===== 怪物 ===== */
            for (int mi = 0; mi < g_monsterCount; mi++) {
                Monster *m = &g_monsters[mi];
                if (!m->alive) continue;
                int mx = (int)(m->x + 0.5f);
                int my = (int)(m->y + 0.5f);
                if (mx == tx && my == ty) {
                    m->hp -= TNT_EXPLODE_DMG;
                    if (m->hp <= 0) {
                        m->alive = 0;
                        on_monster_killed(m);
                    }
                }
            }

            /* ===== 玩家 ===== */
            int px = (int)(playerX + 0.5f);
            int py = (int)(playerY + 0.5f);
            if (px == tx && py == ty) {
                player_take_damage(TNT_EXPLODE_DMG);
            }

            /* ===== 箱子 ===== */
            for (int ci = 0; ci < g_crateCount; ci++) {
                Crate *cr = &g_crates[ci];
                if (!cr->alive) continue;
                if (cr->x == tx && cr->y == ty) {
                    cr->hp -= TNT_EXPLODE_DMG;
                    if (cr->hp <= 0) {
                        cr->alive = 0;
                        g_dungeon.map[ty][tx] = TILE_CRATE_BROKEN;
                        if (dungeon_rand(&g_dungeon, 100) < CRATE_DROP_RATE) {
                            g_dungeon.map[ty][tx] = (dungeon_rand(&g_dungeon, 100) < 50) ?
                                TILE_ENERGY_POTION : TILE_POTION;
                        }
                        g_energy += 3 + dungeon_rand(&g_dungeon, 6);
                        if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                    }
                }
            }

            /* ===== 地刺：收回（无伤害，但玩家/怪物之后踩上去才会受伤）===== */
            if (tile == TILE_SPIKE_UP || tile == TILE_SPIKE_DOWN) {
                /* 收回地刺（变成DOWN状态可安全通过）*/
                g_dungeon.map[ty][tx] = TILE_SPIKE_DOWN;
            }

            /* ===== 另一个TNT：连锁引爆 ===== */
            if (tile == TILE_TNT) {
                for (int ti = 0; ti < MAX_TNTS; ti++) {
                    TNT *ot = &g_tnts[ti];
                    if (ot->state == TNT_IDLE && ot->x == tx && ot->y == ty) {
                        /* 立即连锁引爆（不加额外延迟，用同一帧处理）*/
                        trigger_tnt_explosion(ti);
                        break;
                    }
                }
            }
        }
    }
}

static void check_bullet_hit_monsters(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_bullets[i].active) continue;
        int bx = (int)(g_bullets[i].x + 0.5f);
        int by = (int)(g_bullets[i].y + 0.5f);
        
        /* 先检查是否击中箱子 */
        for (int c = 0; c < g_crateCount; c++) {
            Crate *crate = &g_crates[c];
            if (!crate->alive) continue;
            if (bx == crate->x && by == crate->y) {
                /* 火箭弹击中箱子：触发爆炸（爆炸会破坏箱子）*/
                if (g_bullets[i].type == 2) {
                    trigger_rocket_explosion(g_bullets[i].x, g_bullets[i].y);
                    g_bullets[i].active = 0;
                    goto bulletDone;
                }
                /* 击中箱子 */
                crate->hp -= get_atk_bonus();  /* 技能加成 */
                g_bullets[i].active = 0;  /* 子弹消失 */
                if (crate->hp <= 0) {
                    /* 箱子被破坏 */
                    crate->alive = 0;
                    /* 在地图上标记为已破坏 */
                    g_dungeon.map[by][bx] = TILE_CRATE_BROKEN;
                    /* 30%概率掉落药水 */
                    if (dungeon_rand(&g_dungeon, 100) < CRATE_DROP_RATE) {
                        if (dungeon_rand(&g_dungeon, 100) < 50) {
                            /* 能量药水（15%） */
                            g_dungeon.map[by][bx] = TILE_ENERGY_POTION;
                        } else {
                            /* 生命药水（15%） */
                            g_dungeon.map[by][bx] = TILE_POTION;
                        }
                    }
                    /* 破坏箱子：给3~8随机能量 */
                    {
                        int eGain = 3 + dungeon_rand(&g_dungeon, 6);  /* 3到8 */
                        g_energy += eGain;
                        if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                    }
                }
                goto bulletDone;  /* 子弹已经处理完，跳过怪物检测 */
            }
        }

        /* 检查是否击中TNT */
        if (g_dungeon.map[by][bx] == TILE_TNT) {
            /* 火箭弹击中TNT：先触发火箭爆炸（火箭爆炸中会连锁引爆TNT）*/
            if (g_bullets[i].type == 2) {
                trigger_rocket_explosion(g_bullets[i].x, g_bullets[i].y);
                g_bullets[i].active = 0;
                goto bulletDone;
            }
            /* 找对应TNT，扣血，血量归零才炸 */
            for (int ti = 0; ti < MAX_TNTS; ti++) {
                TNT *t = &g_tnts[ti];
                if (t->state == TNT_IDLE && t->x == bx && t->y == by) {
                    t->hp -= 1;  /* 每颗子弹扣1滴，2滴血共需2发 */
                    if (t->hp <= 0) {
                        /* 血量耗尽：引爆 */
                        trigger_tnt_explosion(ti);
                    }
                    break;
                }
            }
            g_bullets[i].active = 0;
            goto bulletDone;
        }

        /* 再检查是否击中怪物 */
        for (int j = 0; j < g_monsterCount; j++) {
            Monster *m = &g_monsters[j];
            if (!m->alive) continue;
            int mx = (int)(m->x + 0.5f);
            int my = (int)(m->y + 0.5f);
            if (bx == mx && by == my) {
                /* 火箭弹命中怪物：先触发爆炸（爆炸会再对范围内怪物造成伤害）*/
                if (g_bullets[i].type == 2) {
                    trigger_rocket_explosion(g_bullets[i].x, g_bullets[i].y);
                    g_bullets[i].active = 0;
                    goto bulletDone;
                }
                m->hp -= get_atk_bonus();  /* 子弹伤害+技能加成 */
                g_bullets[i].active = 0;
                if (m->hp <= 0) {
                    m->alive = 0;
                    on_monster_killed(m);
                    /* 怪物死亡后在房间中央生成宝箱(可选，暂不生成) */
                    /* Room *r = &g_dungeon.rooms[m->room_idx]; */
                    /* int cx = r->x + r->w / 2; */
                    /* int cy = r->y + r->h / 2; */
                    /* if (g_dungeon.map[cy][cx] == TILE_FLOOR) { */
                    /*     g_dungeon.map[cy][cx] = TILE_CHEST; */
                    /* } */
                }
                break;
            }
        }
    bulletDone:
        ;
    }
}

/* ==================== 绘制怪物 ==================== */
static void draw_monsters(void) {
    int screenCenterX = LCD_WIDTH / 2;
    int screenCenterY = LCD_HEIGHT / 2 - 40;

    for (int i = 0; i < g_monsterCount; i++) {
        Monster *m = &g_monsters[i];
        if (!m->alive) continue;

        int sx = screenCenterX + (int)((m->x - playerX) * CELL_SIZE);
        int sy = screenCenterY + (int)((m->y - playerY) * CELL_SIZE);

        /* 屏幕范围裁剪 */
        if (sx < -CELL_SIZE || sx > LCD_WIDTH + CELL_SIZE ||
            sy < -CELL_SIZE || sy > LCD_HEIGHT + CELL_SIZE) continue;

        int frame = m->animFrame;

        /* ---- 怪物像素精灵 ---- */
        /* 根据怪物类型使用不同颜色 */
        uint16_t bodyColor, headColor;
        if (m->type == MONSTER_TYPE_A) {
            /* A类：绿色（近战型）*/
            bodyColor = 0x0400;   /* 深绿色 */
            headColor = 0x0200;   /* 更绿 */
        } else if (m->type == MONSTER_TYPE_B) {
            /* B类：紫色（巨型子弹型）*/
            bodyColor = 0x8010;   /* 紫色 */
            headColor = 0x4008;   /* 深紫 */
        } else if (m->type == MONSTER_TYPE_D) {
            /* D类：金色（宝箱守卫，小boss）*/
            bodyColor = 0x8400;   /* 金色身体 */
            headColor = 0xFC00;   /* 亮金色头部 */
        } else if (m->type == MONSTER_TYPE_BOSS) {
            /* BOSS：红色带火焰效果，巨大的身体 */
            bodyColor = RED;      /* 红色身体 */
            headColor = 0xF800;   /* 深红色头部，火焰感 */
            /* BOSS有光环 */
            draw_circle_with_map(sx, sy, 12, YELLOW);
        } else if (m->type == MONSTER_TYPE_BOSS2) {
            /* BOSS2：紫色+青色双色光环，紫色主体，第10关专属终极BOSS */
            bodyColor = 0x8010;   /* 紫色身体（和D类一样，但更大） */
            headColor = 0x4008;   /* 深紫色头部 */
            /* BOSS2有双色光环：外圈青色，内圈黄色 */
            draw_circle_with_map(sx, sy, 14, CYAN);   /* 外圈青色大光环 */
            draw_circle_with_map(sx, sy, 10, YELLOW);  /* 内圈黄色光环 */
        } else {
            /* C类：青色（大量子弹型）*/
            bodyColor = 0x7BE0;   /* 青色 */
            headColor = 0x03E0;   /* 深青 */
        }

        /* 身体和头部绘制 */
        if (m->type == MONSTER_TYPE_BOSS) {
            /* BOSS更大更醒目 */
            fill_rect_with_map(sx - 6, sy - 7, sx + 6, sy + 6, bodyColor);
            fill_rect_with_map(sx - 5, sy - 11, sx + 5, sy - 7, headColor);
            /* BOSS的眼睛更大更有气势 */
            if (m->dir == 0) {
                fill_rect_with_map(sx - 3, sy - 9, sx - 1, sy - 8, WHITE);
                fill_rect_with_map(sx + 1, sy - 9, sx + 3, sy - 8, WHITE);
            } else if (m->dir == 1) {
                /* 背面 */
            } else {
                int ex = (m->dir == 2) ? sx - 4 : sx + 3;
                fill_rect_with_map(ex, sy - 9, ex + 2, sy - 8, WHITE);
            }
            /* BOSS的角更大 */
            fill_rect_with_map(sx - 6, sy - 13, sx - 2, sy - 11, 0x6B6B);
            fill_rect_with_map(sx + 2, sy - 13, sx + 6, sy - 11, 0x6B6B);
            /* BOSS的嘴巴更凶 */
            fill_rect_with_map(sx - 3, sy - 5, sx + 3, sy - 4, BLACK);
            /* BOSS的腿部更粗 */
            if (frame == 0) {
                fill_rect_with_map(sx - 3, sy + 6, sx - 1, sy + 9, 0x4208);
                fill_rect_with_map(sx + 1, sy + 6, sx + 3, sy + 9, 0x4208);
            } else if (frame == 1) {
                fill_rect_with_map(sx - 4, sy + 6, sx - 2, sy + 9, 0x4208);
                fill_rect_with_map(sx + 2, sy + 6, sx + 4, sy + 9, 0x4208);
            } else {
                fill_rect_with_map(sx - 2, sy + 6, sx, sy + 9, 0x4208);
                fill_rect_with_map(sx + 1, sy + 6, sx + 3, sy + 9, 0x4208);
            }
        } else if (m->type == MONSTER_TYPE_BOSS2) {
            /* BOSS2：同样大但带紫色色调 */
            fill_rect_with_map(sx - 6, sy - 7, sx + 6, sy + 6, bodyColor);
            fill_rect_with_map(sx - 5, sy - 11, sx + 5, sy - 7, headColor);
            /* BOSS2的眼睛 */
            if (m->dir == 0) {
                fill_rect_with_map(sx - 3, sy - 9, sx - 1, sy - 8, WHITE);
                fill_rect_with_map(sx + 1, sy - 9, sx + 3, sy - 8, WHITE);
            } else if (m->dir == 1) {
                /* 背面不画眼睛 */
            } else {
                int ex = (m->dir == 2) ? sx - 4 : sx + 3;
                fill_rect_with_map(ex, sy - 9, ex + 2, sy - 8, WHITE);
            }
            /* BOSS2的角（金色/黄色）*/
            fill_rect_with_map(sx - 6, sy - 13, sx - 2, sy - 11, YELLOW);
            fill_rect_with_map(sx + 2, sy - 13, sx + 6, sy - 11, YELLOW);
            /* BOSS2的嘴巴 */
            fill_rect_with_map(sx - 3, sy - 5, sx + 3, sy - 4, BLACK);
            /* BOSS2的腿部 */
            if (frame == 0) {
                fill_rect_with_map(sx - 3, sy + 6, sx - 1, sy + 9, 0x4208);
                fill_rect_with_map(sx + 1, sy + 6, sx + 3, sy + 9, 0x4208);
            } else if (frame == 1) {
                fill_rect_with_map(sx - 4, sy + 6, sx - 2, sy + 9, 0x4208);
                fill_rect_with_map(sx + 2, sy + 6, sx + 4, sy + 9, 0x4208);
            } else {
                fill_rect_with_map(sx - 2, sy + 6, sx, sy + 9, 0x4208);
                fill_rect_with_map(sx + 1, sy + 6, sx + 3, sy + 9, 0x4208);
            }
            /* 普通怪物 */
            fill_rect_with_map(sx - 4, sy - 5, sx + 4, sy + 4, bodyColor);
            fill_rect_with_map(sx - 3, sy - 8, sx + 3, sy - 5, headColor);
            /* 角 */
            fill_rect_with_map(sx - 4, sy - 10, sx - 2, sy - 8, 0x6B6B);
            fill_rect_with_map(sx + 2, sy - 10, sx + 4, sy - 8, 0x6B6B);
        }
        /* 眼睛（黄色，根据朝向）*/
        if (m->dir == 0) {         /* 朝下 */
            fill_rect_with_map(sx - 2, sy - 7, sx - 1, sy - 6, YELLOW);
            fill_rect_with_map(sx + 1, sy - 7, sx + 2, sy - 6, YELLOW);
        } else if (m->dir == 1) {  /* 朝上 */
            /* 背面不画眼睛 */
        } else {                   /* 朝左/右 */
            int ex = (m->dir == 2) ? sx - 2 : sx + 1;
            fill_rect_with_map(ex, sy - 7, ex + 1, sy - 6, YELLOW);
        }
        /* 嘴巴（黑色横线）*/
        fill_rect_with_map(sx - 2, sy - 5, sx + 2, sy - 4, BLACK);
        /* 腿部 */
        if (frame == 0) {
            fill_rect_with_map(sx - 2, sy + 4, sx, sy + 7, 0x4208);
            fill_rect_with_map(sx + 1, sy + 4, sx + 3, sy + 7, 0x4208);
        } else if (frame == 1) {
            fill_rect_with_map(sx - 3, sy + 4, sx - 1, sy + 7, 0x4208);
            fill_rect_with_map(sx + 2, sy + 4, sx + 4, sy + 7, 0x4208);
        } else {
            fill_rect_with_map(sx - 1, sy + 4, sx + 1, sy + 7, 0x4208);
        }
        /* 血条（根据怪物类型显示不同最大血量）*/
        int maxHp = MONSTER_A_HP;  /* 默认A类 */
        if (m->type == MONSTER_TYPE_B) maxHp = MONSTER_B_HP;
        else if (m->type == MONSTER_TYPE_C) maxHp = MONSTER_C_HP;
        else if (m->type == MONSTER_TYPE_D) maxHp = MONSTER_D_HP;
        else if (m->type == MONSTER_TYPE_BOSS) maxHp = MONSTER_BOSS_HP;
        else if (m->type == MONSTER_TYPE_BOSS2) maxHp = MONSTER_BOSS2_HP;
        
        if (m->hp < maxHp) {
            /* BOSS血条更宽 */
            int barW = (m->type == MONSTER_TYPE_BOSS || m->type == MONSTER_TYPE_BOSS2) ? 20 : 14;
            int hpW = (m->hp * barW) / maxHp;
            fill_rect_with_map(sx - barW/2, sy - 13, sx + barW/2, sy - 11, 0x4208);
            if (hpW > 0)
                fill_rect_with_map(sx - barW/2, sy - 13, sx - barW/2 + hpW, sy - 11, RED);
        }
    }
}

/* ==================== 绘制怪物子弹 ==================== */
static void draw_mbullets(void) {
    int screenCenterX = LCD_WIDTH / 2;
    int screenCenterY = LCD_HEIGHT / 2 - 40;

    for (int i = 0; i < MAX_MBULLETS; i++) {
        if (!g_mbullets[i].active) continue;

        int sx = screenCenterX + (int)((g_mbullets[i].x - playerX) * CELL_SIZE);
        int sy = screenCenterY + (int)((g_mbullets[i].y - playerY) * CELL_SIZE);

        if (sx < 0 || sx >= LCD_WIDTH || sy < 0 || sy >= LCD_HEIGHT) continue;

        /* 根据子弹类型绘制不同大小的子弹 */
        if (g_mbullets[i].type == MONSTER_TYPE_B) {
            /* B类巨型子弹：更大的红色圆 */
            fill_rect_with_map(sx - 4, sy - 4, sx + 4, sy + 4, RED);
            /* 核心亮点 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, WHITE);
        } else if (g_mbullets[i].type == MONSTER_TYPE_D) {
            /* D类环形子弹：金色子弹 */
            fill_rect_with_map(sx - 3, sy - 3, sx + 3, sy + 3, YELLOW);
            fill_rect_with_map(sx - 1, sy - 1, sx + 1, sy + 1, WHITE);
        } else if (g_mbullets[i].type == MONSTER_TYPE_C) {
            /* C类大量子弹：较小的青色圆 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, CYAN);
        } else if (g_mbullets[i].type == 6) {
            /* BOSS巨大子弹：最大的红色圆，带光环 */
            fill_rect_with_map(sx - 6, sy - 6, sx + 6, sy + 6, RED);
            fill_rect_with_map(sx - 4, sy - 4, sx + 4, sy + 4, 0xFC00);  /* 橙色核心 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, WHITE);
        } else if (g_mbullets[i].type == 7) {
            /* BOSS快速小子弹：暗红色小圆 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, 0x7800);
        } else if (g_mbullets[i].type == 8) {
            /* BOSS最强子弹：紫红色大圆，带闪电效果 */
            fill_rect_with_map(sx - 5, sy - 5, sx + 5, sy + 5, MAGENTA);
            fill_rect_with_map(sx - 3, sy - 3, sx + 3, sy + 3, RED);
            fill_rect_with_map(sx - 1, sy - 1, sx + 1, sy + 1, WHITE);
        } else {
            /* 默认子弹：红色小圆 */
            fill_rect_with_map(sx - 2, sy - 2, sx + 2, sy + 2, RED);
        }
    }
}

/* ==================== 前向声明 ==================== */
static void init_dungeon(void);
static void place_portal(void);
static void place_chests(void);
static void place_start_weapons(void);
static void place_walls(void);
static void place_tnts(void);
static void trigger_tnt_explosion(int tntIdx);
/*
 * 规则：
 * - 出生房间(room 0, ROOM_START)：绝对不放
 * - 战斗/功能房间(ELITE/TREASURE/SHOP/SECRET)：100%放地刺
 * - 普通房间(NORMAL)：60%概率放地刺
 * - BOSS房间：100%放地刺
 * - 走廊：每个走廊格子50%概率放地刺（增加多样性）
 *
 * 房间内分布：靠墙一圈 + 中间零散随机
 * 走廊内分布：随机散布（不铺满）
 */
static void place_spikes(void) {
    int r, c, i;

    for (i = 0; i < g_dungeon.room_count; i++) {
        Room *room = &g_dungeon.rooms[i];

        /* 出生房间绝对不放地刺 */
        if (room->type == ROOM_START) continue;

        /* 普通房间60%概率放 */
        if (room->type == ROOM_NORMAL) {
            if (dungeon_rand(&g_dungeon, 100) >= 60) continue;
        }
        /* 其他类型（ELITE/TREASURE/SHOP/SECRET/BOSS）：100%放 */

        /* --- 靠墙一圈地刺 --- */
        /* 上边一行（房间内最上面一排地板）*/
        for (c = room->x; c < room->x + room->w; c++) {
            int rr = room->y;
            if (rr >= 0 && rr < g_dungeon.height && c >= 0 && c < g_dungeon.width) {
                if (g_dungeon.map[rr][c] == TILE_FLOOR)
                    g_dungeon.map[rr][c] = TILE_SPIKE_DOWN;
            }
        }
        /* 下边一行 */
        for (c = room->x; c < room->x + room->w; c++) {
            int rr = room->y + room->h - 1;
            if (rr >= 0 && rr < g_dungeon.height && c >= 0 && c < g_dungeon.width) {
                if (g_dungeon.map[rr][c] == TILE_FLOOR)
                    g_dungeon.map[rr][c] = TILE_SPIKE_DOWN;
            }
        }
        /* 左边一列 */
        for (r = room->y; r < room->y + room->h; r++) {
            int cc = room->x;
            if (r >= 0 && r < g_dungeon.height && cc >= 0 && cc < g_dungeon.width) {
                if (g_dungeon.map[r][cc] == TILE_FLOOR)
                    g_dungeon.map[r][cc] = TILE_SPIKE_DOWN;
            }
        }
        /* 右边一列 */
        for (r = room->y; r < room->y + room->h; r++) {
            int cc = room->x + room->w - 1;
            if (r >= 0 && r < g_dungeon.height && cc >= 0 && cc < g_dungeon.width) {
                if (g_dungeon.map[r][cc] == TILE_FLOOR)
                    g_dungeon.map[r][cc] = TILE_SPIKE_DOWN;
            }
        }

        /* --- 中间零散地刺（随机30%概率，每个地板格独立判断）--- */
        for (r = room->y + 1; r < room->y + room->h - 1; r++) {
            for (c = room->x + 1; c < room->x + room->w - 1; c++) {
                if (g_dungeon.map[r][c] != TILE_FLOOR) continue;
                if (dungeon_rand(&g_dungeon, 100) < 15) {
                    g_dungeon.map[r][c] = TILE_SPIKE_DOWN;
                }
            }
        }
    }

    /* --- 走廊地刺：每个地板格独立50%概率 --- */
    for (r = 0; r < g_dungeon.height; r++) {
        for (c = 0; c < g_dungeon.width; c++) {
            /* 只处理不在任何房间内的地板格（即走廊） */
            if (g_dungeon.map[r][c] == TILE_FLOOR) {
                if (dungeon_get_room_at(&g_dungeon, c, r) < 0) {
                    /* 这个地板在走廊里，50%放地刺 */
                    if (dungeon_rand(&g_dungeon, 100) < 25) {
                        g_dungeon.map[r][c] = TILE_SPIKE_DOWN;
                    }
                }
            }
        }
    }
}

/* ==================== 战斗房间墙壁放置 ==================== */
/*
 * 规则：
 * - ROOM_START（出生）和 ROOM_EXIT（传送门）：不加
 * - ROOM_NORMAL/ELITE/BOSS：每个房间随机放 2~4 块
 * - 位置要求：
 *   1. 必须是地板 TILE_FLOOR
 *   2. 不能在地刺上（TILE_SPIKE_UP/DOWN）
 *   3. 不能在箱子/宝箱/传送门/地刺上
 *   4. 不能和怪物出生点重叠
 *   5. 距房间边缘至少 1 格（留出走廊）
 */
static void place_walls(void) {
    for (int ri = 0; ri < g_dungeon.room_count; ri++) {
        Room *r = &g_dungeon.rooms[ri];

        /* 出生房间/传送门房间：不加 */
        if (r->type == ROOM_START || r->type == ROOM_EXIT) continue;
        /* 传送门所在房间（g_portalRoom）也不加 */
        if (ri == g_portalRoom) continue;

        /* 每个房间随机 2~4 块墙壁 */
        int wallCount = 2 + dungeon_rand(&g_dungeon, 3);  /* 0~2 + 2 = 2~4 */
        for (int w = 0; w < wallCount; w++) {
            int attempts = 30;  /* 最多试30次找有效位置 */
            while (attempts-- > 0) {
                /* 在房间内（非边缘）随机一个位置 */
                int wx = r->x + 1 + dungeon_rand(&g_dungeon, r->w - 2);
                int wy = r->y + 1 + dungeon_rand(&g_dungeon, r->h - 2);

                /* 必须是地板 */
                if (g_dungeon.map[wy][wx] != TILE_FLOOR) continue;

                /* 不能在地刺上 */
                uint8_t tile = g_dungeon.map[wy][wx];
                if (tile == TILE_SPIKE_UP || tile == TILE_SPIKE_DOWN) continue;

                /* 不能在宝箱/箱子/传送门等特殊格子 */
                if (tile == TILE_CHEST || tile == TILE_CHEST_OPEN) continue;
                if (tile == TILE_CRATE || tile == TILE_CRATE_BROKEN) continue;
                if (tile == TILE_PORTAL) continue;
                if (tile == TILE_POTION || tile == TILE_ENERGY_POTION) continue;
                if (tile == TILE_WEAPON_SHOTGUN || tile == TILE_WEAPON_SMG ||
                    tile == TILE_WEAPON_LASER || tile == TILE_WEAPON_STAFF ||
                    tile == TILE_WEAPON_PISTOL || tile == TILE_WEAPON_ROCKET) continue;

                /* 不能和怪物出生点重叠（怪物生成时会跳过非地板格）*/
                int overlap = 0;
                for (int mi = 0; mi < g_monsterCount; mi++) {
                    if (g_monsters[mi].alive &&
                        (int)g_monsters[mi].x == wx && (int)g_monsters[mi].y == wy) {
                        overlap = 1;
                        break;
                    }
                }
                if (overlap) continue;

                /* 不能和已放箱子重叠 */
                for (int ci = 0; ci < g_crateCount; ci++) {
                    if (g_crates[ci].alive && g_crates[ci].x == wx && g_crates[ci].y == wy)
                        { overlap = 1; break; }
                }
                if (overlap) continue;

                /* 全部检查通过，放置墙壁 */
                g_dungeon.map[wy][wx] = TILE_WALL;
                break;  /* 成功放置，跳出重试循环 */
            }
        }
    }
}

/* ==================== 放置TNT炸药桶 ==================== */
/*
 * 参考place_walls()的写法：全局上限控制，每层固定个数
 * 每层最多放3个TNT（分布在3个不同的随机房间）
 * - 跳过出生房间(ROOM_START)和传送门房间(g_portalRoom)
 * - 跳过边缘格子（紧邻走廊）
 * - 跳过所有非地板格（地刺/墙壁/箱子/传送门/药水/武器等）
 */
#define MAX_TNTS_PER_LEVEL  3   /* 每层最多3个TNT，均匀分布 */
static void place_tnts(void) {
    g_tntCount = 0;

    /* 清空TNT数组 */
    for (int i = 0; i < MAX_TNTS; i++) {
        g_tnts[i].state = TNT_IDLE;
        g_tnts[i].x = 0;
        g_tnts[i].y = 0;
        g_tnts[i].hp = TNT_HP;
        g_tnts[i].explodeStart = 0;
    }
    g_tntAnimActive = 0;

    /* 第一遍：在所有可用房间中，随机选3个各放1个TNT（全局上限控制） */
    int roomsVisited[32] = {0};
    int roomsVisitedCount = 0;

    for (int attempt = 0; attempt < 200 && g_tntCount < MAX_TNTS_PER_LEVEL; attempt++) {
        /* 随机选一个非出生、非传送门的房间 */
        int ri = 1 + dungeon_rand(&g_dungeon, g_dungeon.room_count - 1);
        if (ri == g_dungeon.start_room || ri == g_portalRoom) continue;

        /* 避免同一房间放两个TNT */
        int alreadyChosen = 0;
        for (int k = 0; k < roomsVisitedCount; k++) {
            if (roomsVisited[k] == ri) { alreadyChosen = 1; break; }
        }
        if (alreadyChosen) continue;

        Room *r = &g_dungeon.rooms[ri];
        /* 在房间内（非边缘1格）随机一个位置，尝试30次 */
        for (int tries = 30; tries > 0; tries--) {
            int tx = r->x + 1 + dungeon_rand(&g_dungeon, r->w - 2);
            int ty = r->y + 1 + dungeon_rand(&g_dungeon, r->h - 2);

            /* 必须是地板（墙壁已放，此处只检查TILE_FLOOR即可）*/
            if (g_dungeon.map[ty][tx] != TILE_FLOOR) continue;

            /* 检查是否和已有TNT重复 */
            int dup = 0;
            for (int e = 0; e < g_tntCount; e++) {
                if (g_tnts[e].x == tx && g_tnts[e].y == ty) { dup = 1; break; }
            }
            if (dup) continue;

            /* 跳过传送门格子 */
            if (tx == g_portalX && ty == g_portalY) continue;

            /* 放置TNT */
            TNT *nt = &g_tnts[g_tntCount];
            nt->x = tx;
            nt->y = ty;
            nt->state = TNT_IDLE;
            nt->hp = TNT_HP;
            nt->explodeStart = 0;
            g_dungeon.map[ty][tx] = TILE_TNT;
            g_tntCount++;
            roomsVisited[roomsVisitedCount++] = ri;
            break;
        }
    }
}

/* ==================== 放置箱子 ==================== */
static void place_crates(void) {
    g_crateCount = 0;
    
    /* 清空所有箱子 */
    for (int i = 0; i < MAX_CRATES; i++) {
        g_crates[i].alive = 0;
    }
    
    /* 遍历所有房间（除出生房间和传送门房间外）放置箱子 */
    for (int i = 1; i < g_dungeon.room_count; i++) {
        /* 跳过传送门房间（BOSS在此房间）*/
        if (i == g_portalRoom) continue;
        
        Room *r = &g_dungeon.rooms[i];
        
        /* 每个房间放置7-8个箱子 */
        int crateCount = CRATES_PER_ROOM;
        
        for (int c = 0; c < crateCount && g_crateCount < MAX_CRATES; c++) {
            /* 随机位置（房间内随机，但要避开边缘）*/
            int cx = r->x + 1 + dungeon_rand(&g_dungeon, r->w - 2);
            int cy = r->y + 1 + dungeon_rand(&g_dungeon, r->h - 2);
            
            /* 检查位置是否有效：必须是地板，且不是地刺/传送门/宝箱/TNT等 */
            if (g_dungeon.map[cy][cx] != TILE_FLOOR) continue;
            
            /* 检查是否与现有怪物重叠 */
            int overlapMonster = 0;
            for (int m = 0; m < g_monsterCount; m++) {
                if (g_monsters[m].alive && 
                    (int)g_monsters[m].x == cx && (int)g_monsters[m].y == cy) {
                    overlapMonster = 1;
                    break;
                }
            }
            if (overlapMonster) continue;
            
            /* 检查是否与现有箱子重叠 */
            int overlapCrate = 0;
            for (int k = 0; k < g_crateCount; k++) {
                if (g_crates[k].alive && g_crates[k].x == cx && g_crates[k].y == cy) {
                    overlapCrate = 1;
                    break;
                }
            }
            if (overlapCrate) continue;
            
            /* 检查是否与传送门重叠 */
            if (cx == g_portalX && cy == g_portalY) continue;
            
            /* 放置箱子 */
            Crate *crate = &g_crates[g_crateCount];
            crate->x = cx;
            crate->y = cy;
            crate->alive = 1;
            crate->hp = CRATE_HP;
            /* 掉落在check_bullet_hit_monsters中处理（30%概率）*/
            /* 在地图上标记箱子 */
            g_dungeon.map[cy][cx] = TILE_CRATE;
            
            g_crateCount++;
        }
    }
}

/* ==================== 彩蛋机系统 ==================== */
/* ==================== 放置初始武器 ==================== */

/* 放置单个商品到指定位置 */
static void place_single_item(int idx, int ix, int iy, int itemType, int weaponType) {
    g_itemPos[idx][0] = ix;
    g_itemPos[idx][1] = iy;
    g_itemType[idx] = itemType;
    if (itemType == ITEM_POTION) {
        g_dungeon.map[iy][ix] = TILE_POTION;
        g_itemCost[idx] = calc_shop_item_cost(1, 0);
    } else if (itemType == ITEM_ENERGY_POTION) {
        g_dungeon.map[iy][ix] = TILE_ENERGY_POTION;
        g_itemCost[idx] = calc_shop_item_cost(1, 0);  /* 价格同生命药水 */
    } else {
        /* 地图存TILE_*, 用weapon_to_tile转换 */
        g_dungeon.map[iy][ix] = weapon_to_tile((uint8_t)weaponType);
        g_itemCost[idx] = calc_shop_item_cost(0, weaponType);
    }
}

static void place_start_weapons(void) {
    if (g_dungeon.start_room < 0) return;
    Room *r = &g_dungeon.rooms[g_dungeon.start_room];

    /* ========== 放置商店（左上角2格）========== */
    g_shopX = r->x + 1;
    g_shopY = r->y + 1;
    while (g_shopY < r->y + r->h - 2) {
        if (g_dungeon.map[g_shopY][g_shopX] == TILE_FLOOR &&
            g_dungeon.map[g_shopY][g_shopX + 1] == TILE_FLOOR) {
            g_dungeon.map[g_shopY][g_shopX] = TILE_SHOP_LEFT;
            g_dungeon.map[g_shopY][g_shopX + 1] = TILE_SHOP_RIGHT;
            break;
        }
        g_shopY++;
    }

    /* ========== 放置商店商品（1生命药水+1能量药水+3武器）围绕商店 ========== */
    int itemIdx = 0;
    int candidateX, candidateY;

    /* 优先固定位置：右、右下、下、左、左下 */
    int fixedPos[5][2] = {
        {g_shopX + 2, g_shopY},      /* 右隔1格 */
        {g_shopX + 2, g_shopY + 1},  /* 右下 */
        {g_shopX,     g_shopY + 1},   /* 下 */
        {g_shopX + 1, g_shopY + 1},  /* 左下 */
        {g_shopX + 2, g_shopY - 1},  /* 右上（备用）*/
    };

    /* 商品1：生命药水 - 优先放右侧 */
    if (fixedPos[0][0] >= r->x && fixedPos[0][0] < r->x + r->w &&
        fixedPos[0][1] >= r->y && fixedPos[0][1] < r->y + r->h &&
        g_dungeon.map[fixedPos[0][1]][fixedPos[0][0]] == TILE_FLOOR) {
        place_single_item(itemIdx++, fixedPos[0][0], fixedPos[0][1], ITEM_POTION, 0);
    } else if (find_nearest_floor_around(g_shopX, g_shopY, &candidateX, &candidateY, 2)) {
        place_single_item(itemIdx++, candidateX, candidateY, ITEM_POTION, 0);
    }

    /* 商品2：能量药水 - 优先放下侧 */
    if (fixedPos[2][0] >= r->x && fixedPos[2][0] < r->x + r->w &&
        fixedPos[2][1] >= r->y && fixedPos[2][1] < r->y + r->h &&
        g_dungeon.map[fixedPos[2][1]][fixedPos[2][0]] == TILE_FLOOR &&
        !is_item_pos_taken(fixedPos[2][0], fixedPos[2][1])) {
        place_single_item(itemIdx++, fixedPos[2][0], fixedPos[2][1], ITEM_ENERGY_POTION, 0);
    } else if (find_nearest_floor_around(g_shopX, g_shopY, &candidateX, &candidateY, 3)) {
        place_single_item(itemIdx++, candidateX, candidateY, ITEM_ENERGY_POTION, 0);
    }

    /* 商品3、4、5：3个武器 */
    uint8_t weapons[5] = {WEAPON_SHOTGUN, WEAPON_LASER, WEAPON_STAFF, WEAPON_SMG, WEAPON_ROCKET};
    /* 打乱顺序 */
    for (int i = 4; i > 0; i--) {
        int j = dungeon_rand(&g_dungeon, i + 1);
        uint8_t tmp = weapons[i];
        weapons[i] = weapons[j];
        weapons[j] = tmp;
    }

    for (int w = 0; w < 3 && itemIdx < 5; w++) {
        int placed = 0;
        /* 先试固定优先位置 */
        for (int p = 0; p < 5 && !placed; p++) {
            int px = fixedPos[p][0];
            int py = fixedPos[p][1];
            if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h) {
                if (g_dungeon.map[py][px] == TILE_FLOOR && !is_item_pos_taken(px, py)) {
                    place_single_item(itemIdx++, px, py, ITEM_WEAPON, weapons[w]);
                    placed = 1;
                }
            }
        }
        /* 固定位置都被占了，就在商店周围搜空地 */
        if (!placed && find_nearest_floor_around(g_shopX, g_shopY, &candidateX, &candidateY, 3)) {
            place_single_item(itemIdx++, candidateX, candidateY, ITEM_WEAPON, weapons[w]);
        }
    }

    /* ========== 调试模式：右下角排一行5种武器 ========== */
#if DEBUG_MODE
    {
        uint8_t debugWeapons[6] = {WEAPON_SHOTGUN, WEAPON_LASER, WEAPON_STAFF, WEAPON_SMG, WEAPON_PISTOL, WEAPON_ROCKET};
        /* 从房间右下角开始，向左排一行 */
        int row = r->y + r->h - 2;  /* 房间最底下一行（留一格墙） */
        for (int i = 0; i < 6; i++) {
            int col = r->x + r->w - 2 - i;  /* 从右往左 */
            if (col < r->x + 1) break;
            if (g_dungeon.map[row][col] == TILE_FLOOR && !is_item_pos_taken(col, row)) {
                switch (debugWeapons[i]) {
                    case WEAPON_SHOTGUN: g_dungeon.map[row][col] = TILE_WEAPON_SHOTGUN; break;
                    case WEAPON_LASER:   g_dungeon.map[row][col] = TILE_WEAPON_LASER;   break;
                    case WEAPON_STAFF:   g_dungeon.map[row][col] = TILE_WEAPON_STAFF;   break;
                    case WEAPON_SMG:     g_dungeon.map[row][col] = TILE_WEAPON_SMG;     break;
                    case WEAPON_PISTOL:  g_dungeon.map[row][col] = TILE_WEAPON_PISTOL;  break;
                    case WEAPON_ROCKET:  g_dungeon.map[row][col] = TILE_WEAPON_ROCKET;  break;
                }
            }
        }
    }
#endif

    /* ========== 放置彩蛋机（在房间中央附近）========== */
    int roomCX = r->x + r->w / 2;
    int roomCY = r->y + r->h / 2;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int gx = roomCX + dx;
            int gy = roomCY + dy;
            if (g_dungeon.map[gy][gx] == TILE_FLOOR && !is_item_pos_taken(gx, gy)) {
                g_dungeon.map[gy][gx] = TILE_GACHAPON;
                g_gachaponX = gx;
                g_gachaponY = gy;
                break;
            }
        }
        if (g_gachaponX >= 0) break;
    }
}

/* ==================== 碰撞检测 ==================== */
static int can_move_to(float newX, float newY) {
    int col = (int)(newX + 0.5f);
    int row = (int)(newY + 0.5f);
    
    if (col < 0 || col >= DUNGEON_COLS || row < 0 || row >= DUNGEON_ROWS)
        return 0;
    
    uint8_t tile = g_dungeon.map[row][col];
    
    /* 墙壁、关闭的门、箱子、商店、TNT不可通行（地刺/药水可通行，伤害在update里单独检测）*/
    if (tile == TILE_WALL || tile == TILE_DOOR_CLOSED || tile == TILE_CRATE ||
        tile == TILE_SHOP_LEFT || tile == TILE_SHOP_RIGHT || tile == TILE_TNT)
        return 0;
    
    return 1;
}

/* ==================== 在随机非出生房间放置传送门 ==================== */
static void place_portal(void) {
    if (g_dungeon.room_count < 2) return;
    
    /* 找距离出生点最远的房间，避免传送门出现在出生点旁边 */
    int spawnIdx = g_dungeon.start_room;
    Room *spawn = &g_dungeon.rooms[spawnIdx];
    int spawnCX = spawn->x + spawn->w / 2;
    int spawnCY = spawn->y + spawn->h / 2;
    
    int bestRoom = -1;
    int bestDist = -1;
    
    for (int i = 1; i < g_dungeon.room_count; i++) {  /* 从1开始，避免出生点 */
        Room *r = &g_dungeon.rooms[i];
        int cx = r->x + r->w / 2;
        int cy = r->y + r->h / 2;
        int dist = abs(cx - spawnCX) + abs(cy - spawnCY); /* 曼哈顿距离 */
        if (dist > bestDist) {
            bestDist = dist;
            bestRoom = i;
        }
    }
    
    if (bestRoom < 0) bestRoom = 1;
    
    Room *r = &g_dungeon.rooms[bestRoom];
    g_portalX = r->x + r->w / 2;
    g_portalY = r->y + r->h / 2;
    g_portalRoom = bestRoom;
    
    /* 在地图上标记传送门 */
    g_dungeon.map[g_portalY][g_portalX] = TILE_PORTAL;
}

/* ==================== 放置宝箱/怪物（根据房间类型）==================== */
static void place_chests(void) {
    if (g_dungeon.room_count < 3) return;

    g_monsterCount = 0;
    g_bossDefeated = 0;  /* 重置BOSS状态 */
    
    /* 先清空所有怪物 */
    for (int i = 0; i < MAX_MONSTERS; i++) g_monsters[i].alive = 0;
    
    /* BOSS关卡时，为BOSS预留一个位置（BOSSNUM=5，即第5关和第10关为BOSS关）*/
#define BOSSNUM 5
    int reservedForBoss = (g_dungeonLevel > 0 && g_dungeonLevel % BOSSNUM == 0 && g_dungeonLevel <= 10) ? 1 : 0;

    for (int i = 0; i < g_dungeon.room_count; i++) {
        if (i == g_dungeon.start_room) continue;  /* 出生房间不放任何东西 */

        Room *r = &g_dungeon.rooms[i];
        int cx = r->x + r->w / 2;
        int cy = r->y + r->h / 2;

        /* 传送门房间特殊处理：不管房间类型是什么，都要放怪物！ */
        if (i == g_portalRoom) {
            /* 传送门房间：改成战斗房间，放15个怪 */
            int monsterPerWave = 15;
            
            /* 如果是BOSS关卡，少放一些怪，给BOSS留位置 */
            if (reservedForBoss) {
                monsterPerWave = 10;  /* BOSS关少放几个怪 */
            }
            
            for (int mIdx = 0; mIdx < monsterPerWave && g_monsterCount < MAX_MONSTERS - reservedForBoss; mIdx++) {
                Monster *m = &g_monsters[g_monsterCount];
                
                /* 随机位置（房间内随机）*/
                int mx = r->x + 1 + dungeon_rand(&g_dungeon, r->w - 2);
                int my = r->y + 1 + dungeon_rand(&g_dungeon, r->h - 2);
                
                /* 确保位置是地板（但TNT是特殊的，不在上面生成），且不与传送门重叠 */
                uint8_t tile = g_dungeon.map[my][mx];
                if (tile != TILE_FLOOR) continue;
                if (tile == TILE_TNT) continue;      /* 不生成在TNT上 */
                if (mx == g_portalX && my == g_portalY) continue;
                
                m->x = (float)mx;
                m->y = (float)my;
                m->alive = 1;
                m->room_idx = i;
                m->rx1 = r->x + 1;
                m->ry1 = r->y + 1;
                m->rx2 = r->x + r->w - 2;
                m->ry2 = r->y + r->h - 2;
                m->lastMelee = 0;
                m->lastRanged = 0;
                m->lastRanged2 = 0;
                m->lastRanged3 = 0;
                m->lastRanged4 = 0;
                m->lastRanged5 = 0;
                m->mgStartTick = 0;
                m->dir = 1;
                m->lastAnimTick = 0;
                m->animFrame = 0;

                /* 随机分配怪物类型：A类40%，B类30%，C类30% */
                int typeRand = dungeon_rand(&g_dungeon, 100);
                if (typeRand < 40) {
                    m->type = MONSTER_TYPE_A;
                    m->hp = MONSTER_A_HP;
                } else if (typeRand < 70) {
                    m->type = MONSTER_TYPE_B;
                    m->hp = MONSTER_B_HP;
                } else {
                    m->type = MONSTER_TYPE_C;
                    m->hp = MONSTER_C_HP;
                }
                
                g_monsterCount++;
            }
            
            /* BOSS关卡：在传送门房间生成BOSS */
            if (reservedForBoss && g_monsterCount < MAX_MONSTERS) {
                Monster *m = &g_monsters[g_monsterCount];
                /* BOSS放在传送门旁边的有效地板位置 */
                int bossX = g_portalX;
                int bossY = g_portalY;
                
                /* 从传送门周围搜索一个有效的地板位置 */
                int found = 0;
                for (int dy = -1; dy <= 1 && !found; dy++) {
                    for (int dx = -1; dx <= 1 && !found; dx++) {
                        int tx = g_portalX + dx;
                        int ty = g_portalY + dy;
                        /* 检查边界和是否是地板 */
                        if (tx >= r->x + 1 && tx <= r->x + r->w - 2 &&
                            ty >= r->y + 1 && ty <= r->y + r->h - 2 &&
                            g_dungeon.map[ty][tx] == TILE_FLOOR) {
                            bossX = tx;
                            bossY = ty;
                            found = 1;
                        }
                    }
                }
                
                /* 如果没找到合适位置，放在房间中央 */
                if (!found) {
                    bossX = r->x + r->w / 2;
                    bossY = r->y + r->h / 2;
                }
                
                m->x = (float)bossX;
                m->y = (float)bossY;
                m->alive = 1;
                /* 第10关为BOSS2（100滴血，5种攻击），其余BOSS关为BOSS1（50滴血，3种攻击）*/
                if (g_dungeonLevel == 10) {
                    m->hp = MONSTER_BOSS2_HP;   /* 100滴 */
                    m->type = MONSTER_TYPE_BOSS2;
                } else {
                    m->hp = MONSTER_BOSS_HP;     /* 50滴 */
                    m->type = MONSTER_TYPE_BOSS;
                }
                m->room_idx = i;
                m->rx1 = r->x + 1;
                m->ry1 = r->y + 1;
                m->rx2 = r->x + r->w - 2;
                m->ry2 = r->y + r->h - 2;
                m->lastMelee = 0;
                m->lastRanged = 0;
                m->lastRanged2 = 0;
                m->lastRanged3 = 0;
                m->lastRanged4 = 0;
                m->lastRanged5 = 0;
                m->mgStartTick = 0;
                m->dir = 1;
                m->lastAnimTick = 0;
                m->animFrame = 0;
                g_monsterCount++;
            }
            
            continue;  /* 传送门房间已经处理完，跳过后续判断 */
        }

        /* 根据房间类型决定放置内容 */
        if (r->type == ROOM_TREASURE) {
            /* 宝箱房间：80%放守卫(宝箱守卫)，20%直接放宝箱 */
            if (dungeon_rand(&g_dungeon, 10) < 8 && g_monsterCount < MAX_MONSTERS - reservedForBoss) {
                /* 生成D类宝箱守卫（环形子弹，10滴血，小boss）*/
                Monster *m = &g_monsters[g_monsterCount];
                m->x = (float)cx;
                m->y = (float)cy;
                m->alive = 1;
                m->hp = MONSTER_D_HP;        /* 守卫血量10滴 */
                m->type = MONSTER_TYPE_D;    /* D类宝箱守卫 */
                m->room_idx = i;
                m->rx1 = r->x + 1;
                m->ry1 = r->y + 1;
                m->rx2 = r->x + r->w - 2;
                m->ry2 = r->y + r->h - 2;
                m->lastMelee = 0;
                m->lastRanged = 0;
                m->lastRanged2 = 0;
                m->lastRanged3 = 0;
                m->lastRanged4 = 0;
                m->lastRanged5 = 0;
                m->mgStartTick = 0;
                m->dir = 1;
                m->lastAnimTick = 0;
                m->animFrame = 0;
                g_monsterCount++;
                /* 宝箱守卫房间中央也放个关闭的宝箱 */
                if (g_dungeon.map[cy][cx] == TILE_FLOOR) {
                    g_dungeon.map[cy][cx] = TILE_CHEST;
                }
            } else {
                /* 直接放宝箱 */
                if (g_dungeon.map[cy][cx] == TILE_FLOOR) {
                    g_dungeon.map[cy][cx] = TILE_CHEST;
                }
            }
        } else {
            /* 其他房间（普通/精英/BOSS房间）：放怪物 */
            /* 每波生成10个怪物，随机分配类型 */
            int monsterPerWave = 10;
            for (int mIdx = 0; mIdx < monsterPerWave && g_monsterCount < MAX_MONSTERS - reservedForBoss; mIdx++) {
                Monster *m = &g_monsters[g_monsterCount];
                
                /* 随机位置（房间内随机）*/
                int mx = r->x + 1 + dungeon_rand(&g_dungeon, r->w - 2);
                int my = r->y + 1 + dungeon_rand(&g_dungeon, r->h - 2);
                
                /* 确保位置是地板（但TNT格不行），且不与传送门重叠 */
                uint8_t tile = g_dungeon.map[my][mx];
                if (tile != TILE_FLOOR) continue;
                if (tile == TILE_TNT) continue;      /* 不生成在TNT上 */
                
                m->x = (float)mx;
                m->y = (float)my;
                m->alive = 1;
                m->room_idx = i;
                m->rx1 = r->x + 1;
                m->ry1 = r->y + 1;
                m->rx2 = r->x + r->w - 2;
                m->ry2 = r->y + r->h - 2;
                m->lastMelee = 0;
                m->lastRanged = 0;
                m->lastRanged2 = 0;
                m->lastRanged3 = 0;
                m->lastRanged4 = 0;
                m->lastRanged5 = 0;
                m->mgStartTick = 0;
                m->dir = 1;
                m->lastAnimTick = 0;
                m->animFrame = 0;

                /* 随机分配怪物类型：A类40%，B类30%，C类30% */
                int typeRand = dungeon_rand(&g_dungeon, 100);
                if (typeRand < 40) {
                    /* A类：近战型 */
                    m->type = MONSTER_TYPE_A;
                    m->hp = MONSTER_A_HP;
                } else if (typeRand < 70) {
                    /* B类：巨型远程 */
                    m->type = MONSTER_TYPE_B;
                    m->hp = MONSTER_B_HP;
                } else {
                    /* C类：大量远程 */
                    m->type = MONSTER_TYPE_C;
                    m->hp = MONSTER_C_HP;
                }
                
                g_monsterCount++;
            }
        }
        /* 其他房间类型（START, SHOP, SECRET, EXIT等）不放怪物 */
    }
}

/* ==================== 更新玩家位置 ==================== */
static void update_player(float dt) {
    if (g_levelTransition) return; /* 层切换动画中不移动 */
    
    if (!joystickActive || (joystickDirX == 0.0f && joystickDirY == 0.0f)) return;
    
    float moveX = joystickDirX * PLAYER_SPEED * dt;
    float moveY = joystickDirY * PLAYER_SPEED * dt;
    float newX = playerX + moveX;
    float newY = playerY + moveY;

    if (can_move_to(newX, playerY))
        playerX = newX;
    if (can_move_to(playerX, newY))
        playerY = newY;
    
    int px = (int)(playerX + 0.5f);
    int py = (int)(playerY + 0.5f);

    /* 检查周围1格内是否有宝箱，靠近自动开启 */
    {
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                int cx = px + dx;
                int cy = py + dy;
                if (cx < 0 || cx >= g_dungeon.width || cy < 0 || cy >= g_dungeon.height) continue;
                if (g_dungeon.map[cy][cx] == TILE_CHEST) {
                    g_dungeon.map[cy][cx] = TILE_CHEST_OPEN;
                    g_gold += 8 + (int)(dungeon_rand(&g_dungeon, 8));  /* 随机8~15金币 */
                }
            }
        }
    }
    
    /* 检查是否撞到箱子（箱子会阻挡玩家，无法进入）*/
    /* 注意：箱子只能用子弹攻击，玩家靠近不会破坏箱子 */
    {
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                int cx = px + dx;
                int cy = py + dy;
                if (cx < 0 || cx >= g_dungeon.width || cy < 0 || cy >= g_dungeon.height) continue;
                if (g_dungeon.map[cy][cx] == TILE_CRATE) {
                    /* 箱子阻挡玩家，禁止进入该格子 */
                    /* 简单处理：不修改位置，只是后续碰撞检测会阻止移动 */
                }
            }
        }
    }
    
    /* 检查是否踩到生命药水 - 需要按攻击键拾取 */
    if (g_dungeon.map[py][px] == TILE_POTION) {
        /* 药水在脚下，等待玩家按攻击键饮用 */
        /* 在攻击检测中处理拾取 */
    }
    
    /* 检查是否踩到传送门 */
    if (px == g_portalX && py == g_portalY && !g_levelTransition) {
        /* BOSS关卡必须先击败BOSS才能进入传送门 */
        if (g_dungeonLevel > 0 && g_dungeonLevel % 5 == 0 && g_dungeonLevel <= 10 && !g_bossDefeated) {
            /* 显示提示：BOSS未击败 */
            lcd_show_string(LCD_WIDTH/2 - 60, LCD_HEIGHT/2, 200, 16, 16, "BOSS NOT DEFEATED!", RED);
            delay_ms(800);
            return;  /* 不能进入传送门 */
        }
        
        g_levelTransition = 1;
        
        /* 通关检测：第10关BOSS被击败后，再进传送门 = 通关胜利 */
        if (g_dungeonLevel == 10 && g_bossDefeated) {
            lcd_clear(BLACK);
            lcd_show_string(LCD_WIDTH/2-70, LCD_HEIGHT/2-40, 200, 16, 32, "VICTORY!", YELLOW);
            lcd_show_string(LCD_WIDTH/2-90, LCD_HEIGHT/2+10, 220, 16, 16, "You cleared all 10 levels!", 0x07E0);
            lcd_show_string(LCD_WIDTH/2-60, LCD_HEIGHT/2+40, 140, 16, 16, "GOLD:", CYAN);
            char gb[16]; sprintf(gb, "%d", g_gold);
            lcd_show_string(LCD_WIDTH/2+20, LCD_HEIGHT/2+40, 100, 16, 16, gb, CYAN);
            while (1) delay_ms(1000);  /* 停住 */
        }
        
        g_dungeonLevel++;
        /* 通过地牢：额外获得15能量 */
        g_energy += ENERGY_PER_LEVEL;
        if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
        /* 显示技能选择界面（技能未满时；满员后跳过直接下一关）*/
        {
            lcd_fill(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1, BLACK);
            lcd_show_string(LCD_WIDTH/2-70, LCD_HEIGHT/2,     160, 16, 16, "Level Up!", YELLOW);
            lcd_show_string(LCD_WIDTH/2-80, LCD_HEIGHT/2+30, 180, 16, 16, "+15 Energy", 0x07E0);
            delay_ms(1200);
            if (g_skillCount < MAX_SKILLS) {
                do_skill_selection();
            } else {
                lcd_show_string(LCD_WIDTH/2-70, LCD_HEIGHT/2+60, 200, 16, 16, "Skills Maxed!", RED);
                delay_ms(800);
            }
        }
        /* 播放传送动画 */
        run_transition_animation();
        /* 动画结束后生成新层 */
        init_dungeon();
        place_spikes();
        place_portal();
        place_walls();  /* 战斗房间内加随机墙壁 */
        place_tnts();    /* TNT必须在箱子之前放！箱子会检查TILE_TNT跳过 */
        place_crates();  /* 箱子生成时会跳过TNT格 */
        place_chests();  /* 生成怪物（会跳过TNT格）*/
        place_start_weapons();  /* 每关都放置彩蛋机 */
        {
            int i;
            for (i = 0; i < 5; i++) {
                /* 快速闪烁提示新层开始 */
                lcd_clear(BLACK);
                clearPixelMap();
                render_dungeon();
                draw_player();
                delay_ms(60);
            }
        }
        g_levelTransition = 0;
    }
    
    /* 检查是否进入新房间 */
    int newRoom = dungeon_get_room_at(&g_dungeon, px, py);
    if (newRoom >= 0 && newRoom != g_playerRoom) {
        g_playerRoom = newRoom;
    }
}

/* ==================== 初始化BSP地牢 ==================== */
static void init_dungeon(void) {
    /* 使用BSP算法生成随机地牢 */
    dungeon_init(&g_dungeon, DUNGEON_COLS, DUNGEON_ROWS, HAL_GetTick());
    dungeon_generate(&g_dungeon, 8, 12);

    /* 获取出生点 */
    int spawnX, spawnY;
    dungeon_get_spawn_pos(&g_dungeon, &spawnX, &spawnY);
    playerX = (float)spawnX;
    playerY = (float)spawnY;
    g_playerRoom = 0;
    g_portalRoom = -1;
    g_portalX = 0;
    g_portalY = 0;

    /* 重置彩蛋机状态 */
    g_gachaponX = -1;
    g_gachaponY = -1;
    g_gachaponAnimActive = 0;
    g_gachaponAnimStart = 0;
    g_gachaponResult = 0;
    g_gachaponUsed = 0;  /* 每关重置为未使用 */
    
    /* 重置商店状态 */
    g_shopX = -1;
    g_shopY = -1;
    for (int i = 0; i < 5; i++) {
        g_itemPos[i][0] = -1;
        g_itemPos[i][1] = -1;
        g_itemType[i] = 0;
        g_itemCost[i] = 0;
    }
}

/* ==================== 游戏初始化 ==================== */
void game_init(void) {
    
    lcd_show_string(30, 50 + Y_OFFSET, 200, 16, 16, "STM32 BSP", RED);
    lcd_show_string(30, 70 + Y_OFFSET, 200, 16, 16, "Dungeon Gen", RED);
    lcd_show_string(30, 90 + Y_OFFSET, 200, 16, 16, "Ver 9.0", RED);
    delay_ms(1500);
    
    joystickCenterX = JOYSTICK_OFFSET_X;
    joystickCenterY = LCD_HEIGHT - JOYSTICK_OFFSET_Y;
    knobX = joystickCenterX;
    knobY = joystickCenterY;

    /* 重置能量（满能量） */
    g_energy = ENERGY_MAX;

#if DEBUG_MODE
    /* 调试模式：默认赋予所有技能各1层用于测试 */
    g_skillHpUp       = 1;
    g_skillDefUp      = 1;
    g_skillAtkUp      = 1;
    g_skillDualWield  = 1;
    g_skillStaffSpeed = 1;
    g_skillCount      = 5;
    g_hp  = get_max_hp();   /* 回满血（含加成）*/
    g_def = get_max_def();  /* 回满护盾（含加成）*/
#endif

    /* 初始化BSP地牢 */
    init_dungeon();
    place_spikes();
    place_portal();
    place_walls();  /* 战斗房间内加随机墙壁 */
    place_tnts();    /* TNT必须在箱子之前放！箱子会检查TILE_TNT跳过 */
    place_crates();  /* 箱子生成时会跳过TNT格 */
    place_chests();  /* 生成怪物（会跳过TNT格）*/
    place_start_weapons();  /* 在初始房间放置霰弹枪 */

    lcd_clear(BLACK);
    clearPixelMap();
    
    /* 显示地牢信息 */
    lcd_show_string(10, 10 + Y_OFFSET, 200, 16, 16, "Rooms:", RED);
    lcd_show_num(60, 10 + Y_OFFSET, g_dungeon.room_count, 3,3, RED);
    lcd_show_string(10, 30 + Y_OFFSET, 200, 16, 16, "Seed:", RED);
    lcd_show_num(60, 30 + Y_OFFSET, g_dungeon.rng_state, 10,3, RED);
    
    delay_ms(1500);
}

/* ==================== 死亡动画 ==================== */
static void play_death_animation(void) {
    int cx = LCD_WIDTH / 2;
    int cy = LCD_HEIGHT / 2 - 40;
    int r, i;

    /* 阶段1: 角色闪烁3次（约1.2秒）*/
    for (i = 0; i < 6; i++) {
        delay_ms(200);
        if (i % 2 == 0) {
            lcd_fill(cx - 6, cy - 6, cx + 6, cy + 6, RED);
        } else {
            lcd_fill(cx - 8, cy - 8, cx + 8, cy + 8, BLACK);
        }
    }

    /* 阶段2: 从角色位置向外扩散红色圆环（约0.8秒）*/
    for (r = 0; r <= 30; r += 3) {
        lcd_draw_circle(cx, cy, r, RED);
        delay_ms(50);
    }

    /* 阶段3: 黑屏渐暗 */
    for (r = 240; r > 20; r -= 20) {
        lcd_fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, BLACK);
        delay_ms(30);
    }

    /* 完全黑屏 */
    lcd_fill(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, BLACK);
    delay_ms(300);

    /* 显示 Game Over */
    lcd_show_string(LCD_WIDTH/2 - 48, LCD_HEIGHT/2 - 20 + Y_OFFSET, 200, 32, 32, "GAME OVER", RED);

    /* 停住 */
    while (1) { delay_ms(1000); }
}

/* ==================== 游戏主循环 ==================== */
void game_loop(void) {
    uint32_t lastTime = HAL_GetTick();

    while (1) {
        uint32_t now = HAL_GetTick();
        float dt = (now - lastTime) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastTime = now;

        /* 死亡检测（开发者模式下跳过，自动回满血） */
#if !DEV_MODE
        if (g_hp == 0) {
            play_death_animation();
            while (1) { delay_ms(1000); }  /* 停住 */
        }
#else
        if (g_hp == 0) { g_hp = 5; g_def = 6; }  /* 开发者模式：自动回满 */
#endif

        update_joystick();
        update_player(dt);

        /* KEY0攻击检测 */
        {
            uint8_t key = key_scan(0);
            int px = (int)(playerX + 0.5f);
            int py = (int)(playerY + 0.5f);
            
            /* 计算射击方向（所有武器共用） */
            float dirX = 0.0f, dirY = 0.0f;
            int myRoom = get_player_room();
            float bestDist = 999.0f;
            int found = 0;
            if (myRoom >= 0) {
                for (int mi = 0; mi < g_monsterCount; mi++) {
                    if (!g_monsters[mi].alive) continue;
                    if (g_monsters[mi].room_idx != myRoom) continue;
                    float dx = g_monsters[mi].x - playerX;
                    float dy = g_monsters[mi].y - playerY;
                    float d = sqrtf(dx*dx + dy*dy);
                    if (d < bestDist) {
                        bestDist = d;
                        dirX = dx / d;
                        dirY = dy / d;
                        found = 1;
                    }
                }
            }
            if (!found) {
                if (g_playerDir == 0) { dirY = 1.0f; }
                else if (g_playerDir == 1) { dirY = -1.0f; }
                else if (g_playerDir == 2) { dirX = -1.0f; }
                else { dirX = 1.0f; }
            }
            
            /* 冲锋枪：按住KEY0连发，100ms/发（10发/秒） */
            if (g_currentWeapon == WEAPON_SMG) {
                if (KEY0) {
                    if (now - g_smgLastFire >= 100) {
                        g_smgLastFire = now;
                        fire_smg(dirX, dirY);
                        if (g_skillDualWield) fire_smg(-dirX, -dirY);  /* 双武器：后方同射 */
                    }
                }
            }
            
            /* 法杖：按下KEY0发射一圈子弹，有0.5秒冷却 */
            if (g_currentWeapon == WEAPON_STAFF) {
                /* 使用KEY0宏检测（1=按下，0=未按下）*/
                if (KEY0) {
                    if (now - g_staffLastFire >= (uint32_t)get_staff_cooldown()) {  /* 技能加成 */
                        if (g_energy >= ENERGY_COST_STAFF) {  /* 能量足够才能发射 */
                            g_staffLastFire = now;
                            g_energy -= ENERGY_COST_STAFF;
                            fire_staff(dirX, dirY);
                        }
                    }
                }
            }
            
            /* 火箭炮：按下KEY0发射火箭弹，1.2秒冷却，消耗5能量 */
            if (g_currentWeapon == WEAPON_ROCKET) {
                if (KEY0) {
                    uint32_t rocketCD = ROCKET_COOLDOWN;
                    /* SKILL_STAFF_SPEED也加速火箭炮 */
                    if (g_skillStaffSpeed >= 1) rocketCD = rocketCD * 2 / 3;  /* ×0.67 */
                    if (g_skillStaffSpeed >= 2) rocketCD = rocketCD * 4 / 9;  /* ×0.44 */
                    if (now - g_rocketLastFire >= rocketCD) {
                        if (g_energy >= ENERGY_COST_ROCKET) {
                            g_rocketLastFire = now;
                            g_energy -= ENERGY_COST_ROCKET;
                            fire_rocket(dirX, dirY);
                            if (g_skillDualWield) fire_rocket(-dirX, -dirY);
                        }
                    }
                }
            }
            
            /* 激光炮：按下KEY0开始充能，长按0.3秒后激活光束，松手则关闭 */
            if (g_currentWeapon == WEAPON_LASER) {
                /* 开始充能 */
                if (KEY0 && !g_laserActive) {
                    g_laserChargeTick = now;
                    g_laserActive = 2;  /* 2=充能中 */
                }
                /* 充能0.3秒后激活 */
                if (g_laserActive == 2 && now - g_laserChargeTick >= 300) {
                    if (g_energy < ENERGY_DRAIN_LASER) {
                        g_laserActive = 0;  /* 能量不足，无法激活 */
                    } else {
                        g_laserActive = 1;  /* 1=激活中 */
                        g_laserStartTick = now;  /* 重置伤害计时 */
                        g_laserEnergyTick = now;  /* 重置能量扣减计时 */
                    }
                }
                /* 激光激活时：实时计算方向 + 实时计算终点 + 实时检测伤害 */
                if (g_laserActive == 1) {
                    /* 激光起点跟随玩家 */
                    g_laserStartX = playerX;
                    g_laserStartY = playerY;
                    /* 实时重新计算方向：自动跟踪最近的敌人 */
                    float laserDirX = dirX;  /* 使用当前计算出的方向（已指向最近敌人） */
                    float laserDirY = dirY;
                    /* 重新计算激光终点（每帧更新，跟随目标或撞墙）*/
                    g_laserEndX = g_laserStartX;
                    g_laserEndY = g_laserStartY;
                    float stepX = laserDirX * 0.5f;
                    float stepY = laserDirY * 0.5f;
                    float tx = g_laserStartX;
                    float ty = g_laserStartY;
                    for (int k = 0; k < 200; k++) {
                        tx += stepX;
                        ty += stepY;
                        int ix = (int)(tx + 0.5f);
                        int iy = (int)(ty + 0.5f);
                        if (ix < 0 || ix >= g_dungeon.width || iy < 0 || iy >= g_dungeon.height ||
                            !dungeon_is_walkable(&g_dungeon, ix, iy)) {
                            g_laserEndX = tx - stepX;
                            g_laserEndY = ty - stepY;
                            break;
                        }
                        g_laserEndX = tx;
                        g_laserEndY = ty;
                    }
                    /* 保存当前方向供绘制使用 */
                    g_laserDirX = laserDirX;
                    g_laserDirY = laserDirY;
                    /* 能量消耗：每0.5秒扣2能量，能量耗尽自动关闭 */
                    if (now - g_laserEnergyTick >= 500) {
                        g_laserEnergyTick = now;
                        if (g_energy >= ENERGY_DRAIN_LASER) {
                            g_energy -= ENERGY_DRAIN_LASER;
                        } else {
                            g_energy = 0;
                            g_laserActive = 0;  /* 能量耗尽，关闭激光 */
                        }
                    }
                    /* 松开KEY0则关闭激光 */
                    if (!KEY0) {
                        g_laserActive = 0;
                    }
                    /* 实时激光伤害：每0.5秒对光束上所有怪物和箱子造成伤害 */
                    if (now - g_laserStartTick >= 500) {
                        float dx = g_laserEndX - g_laserStartX;
                        float dy = g_laserEndY - g_laserStartY;
                        float len = sqrtf(dx*dx + dy*dy);
                        if (len > 0.01f) {
                            float nx = dx / len;
                            float ny = dy / len;
                            /* 对怪物造成伤害 */
                            for (int mi = 0; mi < g_monsterCount; mi++) {
                                if (!g_monsters[mi].alive) continue;
                                float mx = g_monsters[mi].x;
                                float my = g_monsters[mi].y;
                                /* 计算怪物到激光光束的垂直距离 */
                                float ex = mx - g_laserStartX;
                                float ey = my - g_laserStartY;
                                float dot = ex*nx + ey*ny;
                                if (dot < 0 || dot > len) continue;
                                float closestX = g_laserStartX + nx * dot;
                                float closestY = g_laserStartY + ny * dot;
                                float dist = sqrtf((mx-closestX)*(mx-closestX) + (my-closestY)*(my-closestY));
                                if (dist < 0.8f) {
                                    g_monsters[mi].hp -= get_laser_dmg();  /* 激光伤害+技能加成 */
                                    if (g_monsters[mi].hp <= 0) {
                                        g_monsters[mi].alive = 0;
                                        on_monster_killed(&g_monsters[mi]);
                                    }
                                }
                            }
                            /* 对激光路径上的箱子造成伤害（破坏箱子）*/
                            for (int k = 0; k < (int)(len + 0.5f); k++) {
                                float checkX = g_laserStartX + nx * k;
                                float checkY = g_laserStartY + ny * k;
                                int ix = (int)(checkX + 0.5f);
                                int iy = (int)(checkY + 0.5f);
                                if (ix >= 0 && ix < g_dungeon.width && iy >= 0 && iy < g_dungeon.height) {
                                    if (g_dungeon.map[iy][ix] == TILE_CRATE) {
                                        /* 破坏箱子，可能掉落药水 */
                                        g_dungeon.map[iy][ix] = TILE_FLOOR;
                                        /* 30%概率掉落药水（能量/生命各半） */
                                        if (dungeon_rand(&g_dungeon, 100) < CRATE_DROP_RATE) {
                                            if (dungeon_rand(&g_dungeon, 100) < 50) {
                                                g_dungeon.map[iy][ix] = TILE_ENERGY_POTION;
                                            } else {
                                                g_dungeon.map[iy][ix] = TILE_POTION;
                                            }
                                        }
                                        /* 破坏箱子：给3~8随机能量 */
                                        {
                                            int eGain = 3 + dungeon_rand(&g_dungeon, 6);
                                            g_energy += eGain;
                                            if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                                        }
                                    }
                                }
                            }
                        }
                        g_laserStartTick = now;
                    }
                }
                /* 充能中松开则取消 */
                if (g_laserActive == 2 && !KEY0) {
                    g_laserActive = 0;
                }
            }
            
            /* 拾取和单次射击逻辑（只在key_scan检测到按下时执行） */
            if (key == KEY0_PRES) {
                if (px >= 0 && px < g_dungeon.width && py >= 0 && py < g_dungeon.height) {
                    uint8_t tile = g_dungeon.map[py][px];

                    /* ========== 优先检查商店商品（付钱购买）========== */
                    int onShopItem = -1;
                    for (int i = 0; i < 5; i++) {
                        if (g_itemPos[i][0] == px && g_itemPos[i][1] == py) {
                            onShopItem = i;
                            break;
                        }
                    }
                    if (onShopItem >= 0) {
                        /* 站在商店商品上，购买（优先于免费拾取）*/
                        int cost = g_itemCost[onShopItem];
                        if (g_gold >= cost) {
                            g_gold -= cost;
                            if (g_itemType[onShopItem] == ITEM_POTION) {
                                /* 购买生命药水 */
                                if (g_hp < HP_MAX) {
                                    g_hp += 1;
                                    if (g_hp > HP_MAX) g_hp = HP_MAX;
                                }
                            } else if (g_itemType[onShopItem] == ITEM_ENERGY_POTION) {
                                /* 购买能量药水：恢复50能量 */
                                if (g_energy < ENERGY_MAX) {
                                    g_energy += ENERGY_POTION_RESTORE;
                                    if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                                }
                            } else {
                                /* 购买武器 */
                                uint8_t weaponTile = g_dungeon.map[py][px];
                                uint8_t newWeapon = WEAPON_PISTOL;
                                switch (weaponTile) {
                                    case TILE_WEAPON_SHOTGUN: newWeapon = WEAPON_SHOTGUN; break;
                                    case TILE_WEAPON_SMG:     newWeapon = WEAPON_SMG;     break;
                                    case TILE_WEAPON_LASER:   newWeapon = WEAPON_LASER;   break;
                                    case TILE_WEAPON_STAFF:   newWeapon = WEAPON_STAFF;   break;
                                    case TILE_WEAPON_PISTOL:  newWeapon = WEAPON_PISTOL;  break;
                                    case TILE_WEAPON_ROCKET:  newWeapon = WEAPON_ROCKET;  break;
                                }
                                /* 直接换枪，不掉落旧武器 */
                                g_currentWeapon = newWeapon;
                            }
                            /* 移除商品 */
                            g_dungeon.map[py][px] = TILE_FLOOR;
                            g_itemPos[onShopItem][0] = -1;
                            g_itemPos[onShopItem][1] = -1;
                        }
                    }
                    /* ========== 彩蛋机 ========== */
                    else if (tile == TILE_GACHAPON) {
                        if (!g_gachaponUsed) {
                            int cost = get_gachapon_cost();
                            if (g_gold >= cost && !g_gachaponAnimActive) {
                                g_gold -= cost;
                                g_gachaponUsed = 1;
                                g_gachaponAnimActive = 1;
                                g_gachaponAnimStart = now;
                                g_gachaponResult = gachapon_roll_weapon();
                            }
                        }
                    }
                    /* ========== 拾取免费药水 ========== */
                    else if (tile == TILE_POTION) {
                        if (g_hp < HP_MAX) {
                            g_hp += 1;
                            if (g_hp > HP_MAX) g_hp = HP_MAX;
                        }
                        g_dungeon.map[py][px] = TILE_FLOOR;
                    } else if (tile == TILE_ENERGY_POTION) {
                        /* 拾取能量药水：恢复50能量 */
                        if (g_energy < ENERGY_MAX) {
                            g_energy += ENERGY_POTION_RESTORE;
                            if (g_energy > ENERGY_MAX) g_energy = ENERGY_MAX;
                        }
                        g_dungeon.map[py][px] = TILE_FLOOR;
                    }
                    /* ========== 拾取免费武器（地上的掉落物）========== */
                    else if (tile == TILE_WEAPON_SHOTGUN) {
                        if (g_currentWeapon != WEAPON_SHOTGUN) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_SHOTGUN;
                        }
                    } else if (tile == TILE_WEAPON_SMG) {
                        if (g_currentWeapon != WEAPON_SMG) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_SMG;
                        }
                    } else if (tile == TILE_WEAPON_LASER) {
                        if (g_currentWeapon != WEAPON_LASER) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_LASER;
                        }
                    } else if (tile == TILE_WEAPON_STAFF) {
                        if (g_currentWeapon != WEAPON_STAFF) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_STAFF;
                        }
                    } else if (tile == TILE_WEAPON_PISTOL) {
                        if (g_currentWeapon != WEAPON_PISTOL) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_PISTOL;
                        }
                    } else if (tile == TILE_WEAPON_ROCKET) {
                        if (g_currentWeapon != WEAPON_ROCKET) {
                            uint8_t oldTile = weapon_to_tile(g_currentWeapon);
                            g_dungeon.map[py][px] = (oldTile != TILE_FLOOR) ? oldTile : TILE_FLOOR;
                            g_currentWeapon = WEAPON_ROCKET;
                        }
                    }
                    /* ========== 单次射击（都不是上面情况时：射击）========== */
                    else {
                        if (g_currentWeapon == WEAPON_SHOTGUN) {
                            fire_shotgun(dirX, dirY);
                            if (g_skillDualWield) fire_shotgun(-dirX, -dirY);  /* 双武器 */
                        } else if (g_currentWeapon == WEAPON_PISTOL) {
                            fire_bullet(dirX, dirY);
                            if (g_skillDualWield) fire_bullet(-dirX, -dirY);  /* 双武器 */
                        }
                    }
                }
            }
        }
        update_bullets(dt);
        check_bullet_hit_monsters();

        /* 每隔1秒切换所有门的状态 */
        if (now - g_lastDoorToggle >= DOOR_TOGGLE_MS) {
            g_lastDoorToggle = now;
            g_doorsOpen = !g_doorsOpen;
            toggle_all_doors(g_doorsOpen);
        }

        /* 每隔1.5秒切换所有地刺状态 */
        if (now - g_lastSpikeToggle >= SPIKE_TOGGLE_MS) {
            g_lastSpikeToggle = now;
            g_spikesUp = !g_spikesUp;
            toggle_all_spikes(g_spikesUp);
        }

        /* 护盾自动恢复 */
        update_def_regen(now);

        /* 地刺伤害检测（每帧检测，但只在spikesUp时生效）*/
        check_spike_damage();

        /* 更新怪物AI + 怪物子弹 */
        update_monsters(dt, now);
        update_mbullets(dt);
        
        /* 更新彩蛋机动画 */
        if (g_gachaponAnimActive) {
            uint32_t elapsed = now - g_gachaponAnimStart;
            if (elapsed >= 1000) {
                /* 动画结束，吐出武器 */
                g_gachaponAnimActive = 0;
                /* 在彩蛋机周围找一个空位放置武器 */
                int placed = 0;
                int dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}}; /* 上下左右 */
                for (int i = 0; i < 4 && !placed; i++) {
                    int wx = g_gachaponX + dirs[i][0];
                    int wy = g_gachaponY + dirs[i][1];
                    if (wx >= 0 && wx < g_dungeon.width && wy >= 0 && wy < g_dungeon.height) {
                        if (g_dungeon.map[wy][wx] == TILE_FLOOR) {
                            g_dungeon.map[wy][wx] = g_gachaponResult;
                            placed = 1;
                        }
                    }
                }
            }
        }
        
        /* 传送门闪烁动画 */
        g_portalFlash++;

        /* 玩家方向和跑动动画 */
        update_player_dir();
        if (g_isMoving) {
            if (now - g_lastAnimTick >= ANIM_FRAME_MS) {
                g_lastAnimTick = now;
                g_animFrame++;
                if (g_animFrame > 2) g_animFrame = 1; /* 1↔2交替 */
            }
        } else {
            g_animFrame = 0; /* 站立 */
        }

        clearPixelMap();
        render_dungeon();
        draw_tnt_explosions();  /* TNT爆炸动画（Dungeon层之上，先画）*/
        draw_monsters();
        draw_player();
        draw_joystick();
        draw_bullets();
        draw_laser();
        draw_mbullets();

        fill_background_from_map();
        draw_tnt_explosions();  /* 爆炸动画画在背景之上、HUD之下，永不被覆盖 */
        draw_hud();

        lastjoystickDirX = joystickDirX;
        lastjoystickDirY = joystickDirY;
        delay_ms(10);
    }
}


    

    
