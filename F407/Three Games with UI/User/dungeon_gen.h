#ifndef __DUNGEON_GEN_H
#define __DUNGEON_GEN_H

#include <stdint.h>

/* ==================== Map Configuration ==================== */
#define DUNGEON_MAX_W      50
#define DUNGEON_MAX_H      40

/* ==================== Tile Types ==================== */
#define TILE_NONE          0
#define TILE_WALL          1
#define TILE_FLOOR         2
#define TILE_DOOR_CLOSED   3
#define TILE_DOOR_OPEN     4
#define TILE_SECRET_WALL   5
#define TILE_WATER         6
#define TILE_PIT           7
#define TILE_PORTAL        8
#define TILE_SPIKE_DOWN    9     /* 地刺 - 收起状态（可安全通过）*/
#define TILE_SPIKE_UP      10    /* 地刺 - 弹出状态（造成伤害）*/
#define TILE_CHEST         11    /* 宝箱 - 关闭 */
#define TILE_CHEST_OPEN    12    /* 宝箱 - 已打开 */
#define TILE_CRATE         13    /* 箱子 - 未破坏 */
#define TILE_CRATE_BROKEN  14    /* 箱子 - 已破坏 */
#define TILE_POTION        15    /* 生命药水 */
#define TILE_WEAPON_SHOTGUN 16   /* 霰弹枪（地面物品）*/
#define TILE_WEAPON_SMG     17   /* 冲锋枪（地面物品）*/
#define TILE_WEAPON_LASER   18   /* 激光炮（地面物品）*/
#define TILE_WEAPON_STAFF   19   /* 法杖（地面物品）*/
#define TILE_WEAPON_PISTOL  20   /* 手枪（地面物品，不消耗能量）*/
#define TILE_GACHAPON       21   /* 随机彩蛋机 */
#define TILE_SHOP_LEFT      22   /* 商店左格 */
#define TILE_SHOP_RIGHT     23   /* 商店右格 */
#define TILE_ENERGY_POTION  24   /* 能量药水（蓝色）*/
#define TILE_TNT            25   /* TNT炸药桶（可被引爆）*/
#define TILE_WEAPON_ROCKET   26   /* 火箭炮（地面物品）*/

/* ==================== Room Types ==================== */
#define ROOM_NONE          0
#define ROOM_START         1
#define ROOM_NORMAL        2
#define ROOM_ELITE         3
#define ROOM_TREASURE      4
#define ROOM_SHOP          5
#define ROOM_BOSS          6
#define ROOM_SECRET        7
#define ROOM_EXIT          8

/* ==================== BSP Node ==================== */
typedef struct BSPNode {
    int x, y;
    int w, h;
    int split_horiz;      /* 1=horiz, 0=vert, -1=leaf */
    int split_pos;
    struct BSPNode *left;
    struct BSPNode *right;
    int room_id;
} BSPNode;

/* ==================== Room ==================== */
typedef struct Room {
    int x, y;
    int w, h;
    int type;
    int connected;
    int num_doors;
    int door_pos[4][2];
} Room;

/* ==================== Dungeon ==================== */
typedef struct Dungeon {
    uint8_t map[DUNGEON_MAX_H][DUNGEON_MAX_W];
    Room rooms[32];
    int room_count;
    int width, height;
    int start_room;
    int exit_room;
    BSPNode *bsp_root;
    unsigned rng_state;
} Dungeon;

/* ==================== API ==================== */
void dungeon_init(Dungeon *d, int w, int h, unsigned seed);
int  dungeon_generate(Dungeon *d, int min_room_size, int max_room_size);
int  dungeon_get_spawn_pos(Dungeon *d, int *x, int *y);
int  dungeon_get_room_at(Dungeon *d, int x, int y);
int  dungeon_is_walkable(Dungeon *d, int x, int y);
int  dungeon_open_door(Dungeon *d, int x, int y);
void dungeon_cleanup(Dungeon *d);
int  dungeon_rand(Dungeon *d, int max);

#endif
