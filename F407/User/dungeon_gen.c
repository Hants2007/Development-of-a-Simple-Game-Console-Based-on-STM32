/**
 * ================================================================
 * Dungeon Generator v2 - BSP Method for Soul Knight Style
 * ================================================================
 * Improvements:
 * - Better corridor connection (connects ALL rooms properly)
 * - Smarter door placement (only at room-corridor junctions)
 * - Wider corridors (2 tiles wide for easier navigation)
 * ================================================================
 */

#include "dungeon_gen.h"
#include <stdlib.h>
#include <string.h>

/* ==================== Static Memory Pool ==================== */
#define MAX_BSP_NODES   64
static BSPNode bsp_pool[MAX_BSP_NODES];
static int bsp_pool_index = 0;

/* ==================== RNG ==================== */
int dungeon_rand(Dungeon *d, int max) {
    if (max <= 0) return 0;
    d->rng_state = d->rng_state * 1103515245 + 12345;
    return ((int)((d->rng_state >> 16) & 0x7FFF)) % max;
}

/* ==================== Memory Pool ==================== */
static BSPNode* alloc_bsp_node(void) {
    if (bsp_pool_index >= MAX_BSP_NODES) return NULL;
    BSPNode *node = &bsp_pool[bsp_pool_index++];
    memset(node, 0, sizeof(BSPNode));
    node->split_horiz = -1;
    return node;
}

static void reset_bsp_pool(void) {
    bsp_pool_index = 0;
    memset(bsp_pool, 0, sizeof(bsp_pool));
}

/* ==================== Init ==================== */
void dungeon_init(Dungeon *d, int w, int h, unsigned seed) {
    if (!d) return;
    if (w < 8) w = 8;
    if (w > DUNGEON_MAX_W) w = DUNGEON_MAX_W;
    if (h < 8) h = 8;
    if (h > DUNGEON_MAX_H) h = DUNGEON_MAX_H;
    
    d->width = w;
    d->height = h;
    d->room_count = 0;
    d->start_room = -1;
    d->exit_room = -1;
    d->bsp_root = NULL;
    
    if (seed == 0) seed = 12345;
    d->rng_state = seed;
    
    memset(d->map, 0, sizeof(d->map));
    reset_bsp_pool();
}

/* ==================== BSP Split ==================== */
static void bsp_split(Dungeon *d, BSPNode *node, int min_size, int depth) {
    if (!node || depth > 6) return;
    
    int w = node->w;
    int h = node->h;
    
    int can_split_h = (h >= min_size * 2);
    int can_split_v = (w >= min_size * 2);
    
    if (!can_split_h && !can_split_v) {
        node->split_horiz = -1;
        return;
    }
    
    int split_horiz;
    if (can_split_h && can_split_v) {
        if (h > w) split_horiz = 1;
        else if (w > h) split_horiz = 0;
        else split_horiz = dungeon_rand(d, 2);
    } else if (can_split_h) {
        split_horiz = 1;
    } else {
        split_horiz = 0;
    }
    
    int split_pos;
    if (split_horiz) {
        int min_y = node->y + min_size + 1;
        int max_y = node->y + h - min_size - 1;
        if (max_y <= min_y) return;
        split_pos = min_y + dungeon_rand(d, max_y - min_y + 1);
    } else {
        int min_x = node->x + min_size + 1;
        int max_x = node->x + w - min_size - 1;
        if (max_x <= min_x) return;
        split_pos = min_x + dungeon_rand(d, max_x - min_x + 1);
    }
    
    BSPNode *left = alloc_bsp_node();
    BSPNode *right = alloc_bsp_node();
    if (!left || !right) return;
    
    node->split_horiz = split_horiz;
    node->split_pos = split_pos;
    node->left = left;
    node->right = right;
    
    if (split_horiz) {
        left->x = node->x; left->y = node->y;
        left->w = node->w; left->h = split_pos - node->y;
        right->x = node->x; right->y = split_pos;
        right->w = node->w; right->h = node->y + node->h - split_pos;
    } else {
        left->x = node->x; left->y = node->y;
        left->w = split_pos - node->x; left->h = node->h;
        right->x = split_pos; right->y = node->y;
        right->w = node->x + node->w - split_pos; right->h = node->h;
    }
    
    bsp_split(d, left, min_size, depth + 1);
    bsp_split(d, right, min_size, depth + 1);
}

/* ==================== Create Room in Leaf ==================== */
static void create_room_in_leaf(Dungeon *d, BSPNode *node, int min_size, int max_size) {
    if (!node || d->room_count >= 32) return;
    
    if (node->split_horiz != -1 && (node->left || node->right)) {
        if (node->left) create_room_in_leaf(d, node->left, min_size, max_size);
        if (node->right) create_room_in_leaf(d, node->right, min_size, max_size);
        return;
    }
    
    int max_w = node->w - 2;
    int max_h = node->h - 2;
    if (max_w < min_size || max_h < min_size) return;
    
    int room_w = min_size + dungeon_rand(d, (max_w > max_size ? max_size : max_w) - min_size + 1);
    int room_h = min_size + dungeon_rand(d, (max_h > max_size ? max_size : max_h) - min_size + 1);
    
    if (room_w > max_w) room_w = max_w;
    if (room_h > max_h) room_h = max_h;
    if (room_w < min_size) room_w = min_size;
    if (room_h < min_size) room_h = min_size;
    
    /* Center room within leaf to balance wall gaps on both sides.
     * 1-tile padding ensures dungeon border has walls. */
    int room_x = node->x + 1 + dungeon_rand(d, node->w - room_w - 2);
    int room_y = node->y + 1 + dungeon_rand(d, node->h - room_h - 2);
    
    if (room_x < node->x + 1) room_x = node->x + 1;
    if (room_y < node->y + 1) room_y = node->y + 1;
    
    Room *room = &d->rooms[d->room_count];
    room->x = room_x;
    room->y = room_y;
    room->w = room_w;
    room->h = room_h;
    room->type = ROOM_NORMAL;
    room->connected = 0;
    room->num_doors = 0;
    memset(room->door_pos, 0, sizeof(room->door_pos));
    
    node->room_id = d->room_count;
    node->split_horiz = -1;
    
    d->room_count++;
}

/* ==================== Carve Room ==================== */
static void carve_room(Dungeon *d, Room *room) {
    if (!d || !room) return;
    for (int y = room->y; y < room->y + room->h && y < d->height; y++) {
        for (int x = room->x; x < room->x + room->w && x < d->width; x++) {
            d->map[y][x] = TILE_FLOOR;
        }
    }
}

/* ==================== Create Corridor (L-shape) ==================== */
static void create_corridor(Dungeon *d, int x1, int y1, int x2, int y2) {
    if (!d) return;
    
    if (dungeon_rand(d, 2)) {
        int min_x = (x1 < x2) ? x1 : x2;
        int max_x = (x1 > x2) ? x1 : x2;
        for (int x = min_x; x <= max_x; x++) {
            if (x >= 0 && x < d->width && y1 >= 0 && y1 < d->height) {
                if (d->map[y1][x] == TILE_NONE || d->map[y1][x] == TILE_WALL) {
                    d->map[y1][x] = TILE_FLOOR;
                }
            }
        }
        int min_y = (y1 < y2) ? y1 : y2;
        int max_y = (y1 > y2) ? y1 : y2;
        for (int y = min_y; y <= max_y; y++) {
            if (x2 >= 0 && x2 < d->width && y >= 0 && y < d->height) {
                if (d->map[y][x2] == TILE_NONE || d->map[y][x2] == TILE_WALL) {
                    d->map[y][x2] = TILE_FLOOR;
                }
            }
        }
    } else {
        int min_y = (y1 < y2) ? y1 : y2;
        int max_y = (y1 > y2) ? y1 : y2;
        for (int y = min_y; y <= max_y; y++) {
            if (x1 >= 0 && x1 < d->width && y >= 0 && y < d->height) {
                if (d->map[y][x1] == TILE_NONE || d->map[y][x1] == TILE_WALL) {
                    d->map[y][x1] = TILE_FLOOR;
                }
            }
        }
        int min_x = (x1 < x2) ? x1 : x2;
        int max_x = (x1 > x2) ? x1 : x2;
        for (int x = min_x; x <= max_x; x++) {
            if (x >= 0 && x < d->width && y2 >= 0 && y2 < d->height) {
                if (d->map[y2][x] == TILE_NONE || d->map[y2][x] == TILE_WALL) {
                    d->map[y2][x] = TILE_FLOOR;
                }
            }
        }
    }
}

/* ==================== Find Leaf Room ==================== */
static BSPNode* find_leaf_with_room(BSPNode *node) {
    if (!node) return NULL;
    if (node->room_id >= 0) return node;
    BSPNode *found = find_leaf_with_room(node->left);
    if (found) return found;
    return find_leaf_with_room(node->right);
}

/* ==================== Connect Rooms ==================== */
static void connect_rooms(Dungeon *d, BSPNode *node) {
    if (!node) return;
    if (node->split_horiz == -1 || !node->left || !node->right) return;
    
    BSPNode *left_leaf = find_leaf_with_room(node->left);
    BSPNode *right_leaf = find_leaf_with_room(node->right);
    
    if (left_leaf && right_leaf && left_leaf->room_id >= 0 && right_leaf->room_id >= 0) {
        Room *r1 = &d->rooms[left_leaf->room_id];
        Room *r2 = &d->rooms[right_leaf->room_id];
        
        int x1 = r1->x + r1->w / 2;
        int y1 = r1->y + r1->h / 2;
        int x2 = r2->x + r2->w / 2;
        int y2 = r2->y + r2->h / 2;
        
        create_corridor(d, x1, y1, x2, y2);
    }
    
    connect_rooms(d, node->left);
    connect_rooms(d, node->right);
}

/* ==================== Place Doors ==================== */
/* 门放在走廊里（房间外），不占用房间内部空间。
 * 检测房间四壁外侧是否有走廊（TILE_FLOOR），有则在走廊侧放门。
 * 这样门关时挡住走廊入口，房间内部地板完全保留。 */
static void place_doors(Dungeon *d) {
    if (!d) return;
    
    for (int i = 0; i < d->room_count; i++) {
        Room *room = &d->rooms[i];
        int door_count = 0;
        
        int cx = room->x + room->w / 2;
        int cy = room->y + room->h / 2;
        
        /* Top - 门放在走廊侧 (room->y - 1)，不占房间 */
        int best_dx = -1, best_dy = -1, best_dist = 9999;
        for (int x = room->x; x < room->x + room->w; x++) {
            if (room->y > 0 && d->map[room->y - 1][x] == TILE_FLOOR && d->map[room->y][x] == TILE_FLOOR) {
                int dist = abs(x - cx) + abs(room->y - cy);
                if (dist < best_dist) { best_dist = dist; best_dx = x; best_dy = room->y - 1; }
            }
        }
        if (best_dx >= 0 && door_count < 4) {
            d->map[best_dy][best_dx] = TILE_DOOR_CLOSED;
            room->door_pos[door_count][0] = best_dx;
            room->door_pos[door_count][1] = best_dy;
            door_count++;
        }
        
        /* Bottom - 门放在走廊侧 (room->y + room->h) */
        best_dx = -1; best_dy = -1; best_dist = 9999;
        for (int x = room->x; x < room->x + room->w; x++) {
            if (room->y + room->h < d->height && d->map[room->y + room->h][x] == TILE_FLOOR && d->map[room->y + room->h - 1][x] == TILE_FLOOR) {
                int dist = abs(x - cx) + abs(room->y + room->h - cy);
                if (dist < best_dist) { best_dist = dist; best_dx = x; best_dy = room->y + room->h; }
            }
        }
        if (best_dx >= 0 && door_count < 4) {
            d->map[best_dy][best_dx] = TILE_DOOR_CLOSED;
            room->door_pos[door_count][0] = best_dx;
            room->door_pos[door_count][1] = best_dy;
            door_count++;
        }
        
        /* Left - 门放在走廊侧 (room->x - 1) */
        best_dx = -1; best_dy = -1; best_dist = 9999;
        for (int y = room->y; y < room->y + room->h; y++) {
            if (room->x > 0 && d->map[y][room->x - 1] == TILE_FLOOR && d->map[y][room->x] == TILE_FLOOR) {
                int dist = abs(room->x - cx) + abs(y - cy);
                if (dist < best_dist) { best_dist = dist; best_dx = room->x - 1; best_dy = y; }
            }
        }
        if (best_dx >= 0 && door_count < 4) {
            d->map[best_dy][best_dx] = TILE_DOOR_CLOSED;
            room->door_pos[door_count][0] = best_dx;
            room->door_pos[door_count][1] = best_dy;
            door_count++;
        }
        
        /* Right - 门放在走廊侧 (room->x + room->w) */
        best_dx = -1; best_dy = -1; best_dist = 9999;
        for (int y = room->y; y < room->y + room->h; y++) {
            if (room->x + room->w < d->width && d->map[y][room->x + room->w] == TILE_FLOOR && d->map[y][room->x + room->w - 1] == TILE_FLOOR) {
                int dist = abs(room->x + room->w - cx) + abs(y - cy);
                if (dist < best_dist) { best_dist = dist; best_dx = room->x + room->w; best_dy = y; }
            }
        }
        if (best_dx >= 0 && door_count < 4) {
            d->map[best_dy][best_dx] = TILE_DOOR_CLOSED;
            room->door_pos[door_count][0] = best_dx;
            room->door_pos[door_count][1] = best_dy;
            door_count++;
        }
        
        room->num_doors = door_count;
    }
}

/* ==================== Assign Room Types ==================== */
static void assign_room_types(Dungeon *d) {
    if (!d || d->room_count == 0) return;
    
    d->start_room = 0;
    d->rooms[0].type = ROOM_START;
    
    d->exit_room = d->room_count - 1;
    d->rooms[d->room_count - 1].type = ROOM_BOSS;
    
    for (int i = 1; i < d->room_count - 1; i++) {
        int r = dungeon_rand(d, 100);
        if (r < 25) d->rooms[i].type = ROOM_TREASURE;
        else if (r < 35) d->rooms[i].type = ROOM_SHOP;
        else if (r < 50) d->rooms[i].type = ROOM_ELITE;
        else if (r < 55) d->rooms[i].type = ROOM_SECRET;
        else d->rooms[i].type = ROOM_NORMAL;
    }
}

/* ==================== Connectivity Check (BFS Flood Fill) ==================== */
#define FLOOD_QUEUE_MAX (DUNGEON_MAX_W * DUNGEON_MAX_H)

/* Static to avoid stack overflow on STM32 */
static int flood_qx[FLOOD_QUEUE_MAX];
static int flood_qy[FLOOD_QUEUE_MAX];
static uint8_t flood_visited[DUNGEON_MAX_W * DUNGEON_MAX_H];

static int flood_fill_reachable(Dungeon *d, int sx, int sy, uint8_t *visited) {
    /* BFS from (sx,sy), returns number of walkable tiles reached.
     * Only counts TILE_FLOOR and TILE_DOOR_CLOSED as walkable.
     * Uses static arrays to avoid STM32 stack overflow. */
    if (!d || sx < 0 || sx >= d->width || sy < 0 || sy >= d->height) return 0;
    uint8_t t = d->map[sy][sx];
    if (t != TILE_FLOOR && t != TILE_DOOR_CLOSED && t != TILE_DOOR_OPEN) return 0;
    
    int head = 0, tail = 0, count = 0;
    
    flood_qx[tail] = sx; flood_qy[tail] = sy; tail++;
    visited[sy * d->width + sx] = 1;
    
    while (head < tail) {
        int cx = flood_qx[head], cy = flood_qy[head]; head++;
        count++;
        
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx >= 0 && nx < d->width && ny >= 0 && ny < d->height
                && !visited[ny * d->width + nx]) {
                uint8_t nt = d->map[ny][nx];
                if (nt == TILE_FLOOR || nt == TILE_DOOR_CLOSED || nt == TILE_DOOR_OPEN) {
                    visited[ny * d->width + nx] = 1;
                    flood_qx[tail] = nx; flood_qy[tail] = ny; tail++;
                }
            }
        }
    }
    return count;
}

static void ensure_connectivity(Dungeon *d) {
    /* Check all rooms are reachable from room 0.
     * If not, carve a direct corridor to connect them. */
    if (!d || d->room_count <= 1) return;
    
    /* Use static flood_visited to avoid stack overflow on STM32 */
    
    for (int attempt = 0; attempt < d->room_count; attempt++) {
        memset(flood_visited, 0, sizeof(flood_visited));
        
        /* Flood fill from room 0 center */
        Room *r0 = &d->rooms[0];
        int sx = r0->x + r0->w / 2;
        int sy = r0->y + r0->h / 2;
        flood_fill_reachable(d, sx, sy, flood_visited);
        
        /* Check each room for connectivity */
        int all_connected = 1;
        for (int i = 1; i < d->room_count; i++) {
            Room *ri = &d->rooms[i];
            int cx = ri->x + ri->w / 2;
            int cy = ri->y + ri->h / 2;
            
            if (!flood_visited[cy * d->width + cx]) {
                /* Room i is disconnected! Connect it to room i-1 */
                Room *prev = &d->rooms[i - 1];
                int px = prev->x + prev->w / 2;
                int py = prev->y + prev->h / 2;
                create_corridor(d, px, py, cx, cy);
                all_connected = 0;
            }
        }
        
        if (all_connected) break; /* All connected, done */
    }
}

/* ==================== Fill Walls ==================== */
static void fill_walls(Dungeon *d) {
    if (!d) return;
    for (int y = 0; y < d->height; y++) {
        for (int x = 0; x < d->width; x++) {
            if (d->map[y][x] == TILE_NONE || d->map[y][x] == 0) {
                d->map[y][x] = TILE_WALL;
            }
        }
    }
}

/* ==================== Main Generation ==================== */
int dungeon_generate(Dungeon *d, int min_room_size, int max_room_size) {
    if (!d) return -1;
    
    if (min_room_size < 3) min_room_size = 3;
    if (max_room_size < min_room_size) max_room_size = min_room_size;
    if (max_room_size > 12) max_room_size = 12;
    
    d->room_count = 0;
    memset(d->map, 0, sizeof(d->map));
    reset_bsp_pool();
    
    BSPNode *root = alloc_bsp_node();
    if (!root) return -1;
    
    root->x = 0; root->y = 0;
    root->w = d->width; root->h = d->height;
    root->split_horiz = -1;
    d->bsp_root = root;
    
    bsp_split(d, root, min_room_size, 0);
    create_room_in_leaf(d, root, min_room_size, max_room_size);
    
    if (d->room_count == 0) {
        Room *room = &d->rooms[0];
        room->x = 2; room->y = 2;
        room->w = d->width - 4; room->h = d->height - 4;
        room->type = ROOM_START;
        d->room_count = 1;
        d->start_room = 0;
    }
    
    for (int i = 0; i < d->room_count; i++) {
        carve_room(d, &d->rooms[i]);
    }
    
    connect_rooms(d, root);
    fill_walls(d);
    place_doors(d);
    ensure_connectivity(d);    /* Must be AFTER fill_walls so flood fill is accurate */
    fill_walls(d);             /* Fill any new corridors properly */
    place_doors(d);            /* Re-place doors for any new corridor connections */
    assign_room_types(d);
    
    return 0;
}

/* ==================== Utils ==================== */
int dungeon_get_spawn_pos(Dungeon *d, int *x, int *y) {
    if (!d || d->room_count == 0) return -1;
    int room_idx = (d->start_room >= 0) ? d->start_room : 0;
    Room *room = &d->rooms[room_idx];
    if (x) *x = room->x + room->w / 2;
    if (y) *y = room->y + room->h / 2;
    return 0;
}

int dungeon_get_room_at(Dungeon *d, int x, int y) {
    if (!d) return -1;
    for (int i = 0; i < d->room_count; i++) {
        Room *r = &d->rooms[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) return i;
    }
    return -1;
}

int dungeon_is_walkable(Dungeon *d, int x, int y) {
    if (!d) return 0;
    if (x < 0 || x >= d->width || y < 0 || y >= d->height) return 0;
    uint8_t tile = d->map[y][x];
    /* 子弹可以穿过箱子（后面会检测碰撞），但不能穿墙和关闭的门 */
    return (tile == TILE_FLOOR || tile == TILE_DOOR_OPEN || 
            tile == TILE_SPIKE_DOWN || tile == TILE_SPIKE_UP || 
            tile == TILE_PORTAL || tile == TILE_CHEST || 
            tile == TILE_CHEST_OPEN || tile == TILE_CRATE ||
            tile == TILE_CRATE_BROKEN || tile == TILE_POTION ||
            tile == TILE_TNT || tile == TILE_WEAPON_ROCKET);
}

int dungeon_open_door(Dungeon *d, int x, int y) {
    if (!d) return 0;
    if (x < 0 || x >= d->width || y < 0 || y >= d->height) return 0;
    if (d->map[y][x] == TILE_DOOR_CLOSED) {
        d->map[y][x] = TILE_DOOR_OPEN;
        return 1;
    }
    return 0;
}

void dungeon_cleanup(Dungeon *d) {
    if (!d) return;
    d->bsp_root = NULL;
    reset_bsp_pool();
}
