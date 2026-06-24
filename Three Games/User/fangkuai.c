/**
 ****************************************************************************************************
 * @file        main.c
 * @author      ����ԭ���Ŷ�(ALIENTEK) & �û��޸�
 * @version     V2.2 (�򻯰�)
 * @brief       ����˹������Ϸ�����������ƣ�- �̶���Ϸ���򣬰�ť����ͷ������
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/TOUCH/touch.h"
#include "fangkuai.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==================== �Զ�����ɫ ==================== */
#ifndef LIGHTGRAY
#define LIGHTGRAY   0xC618      /* ǳ��ɫ RGB565 */
#endif
#ifndef DARKGRAY
#define DARKGRAY    0x7BEF      /* ���ɫ RGB565 */
#endif

/* ==================== �̶���Ϸ����������ɵ����� ==================== */
#define BOARD_WIDTH     15                      /* ��Ϸ���ȣ������� */
#define BOARD_HEIGHT    23                      /* ��Ϸ��߶ȣ������� */
#define GRID_SIZE       16                       /* ÿ�����ӵ����ش�С���̶��� */
#define BOARD_LEFT      10                       /* ��Ϸ������߾ࣨ�̶��� */
#define BOARD_TOP       20                       /* ��Ϸ�����ϱ߾� */
#define RIGHT_MARGIN    120                      /* �Ҳ�Ԥ�������ʾ����� */
#define BORDER_WIDTH    2                        /* ��Ϸ������߿��� */

#define BUTTON_AREA_H   80                       /* �ײ���������߶ȣ����أ� */
#define FALL_INTERVAL   500                      /* �Զ�����������룩 */

/* ==================== ��ɫ���� ==================== */
#define COLOR_BG        WHITE                    /* ����ɫ */
#define COLOR_BORDER    BLACK                    /* ��Ϸ������߿���ɫ */
#define COLOR_BUTTON_BG GRAY                      /* ��ť����ɫ */
#define COLOR_BUTTON_TXT BLACK                    /* ��ťͼ����ɫ */
#define COLOR_BUTTON_BORDER DARKGRAY              /* ��ť�߿���ɫ */
#define COLOR_SCORE     BLUE                      /* ������ɫ */
#define COLOR_HIGHLIGHT LIGHTGRAY                 /* �߹���ɫ����������Ч���� */
#define COLOR_SHADOW    DARKGRAY                   /* ��Ӱ��ɫ */

/* ==================== ������״���� ==================== */
static const char shapes[7][4][4][4] = {
    /* I */ { {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
              {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
              {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
              {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}} },
    /* O */ { {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,1,0},{0,1,1,0},{0,0,0,0}} },
    /* T */ { {{0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,1,0,0}},
              {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,1,0,0}},
              {{0,0,0,0},{0,1,0,0},{1,1,0,0},{0,1,0,0}} },
    /* L */ { {{0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,0,0},{0,1,0,0},{0,1,1,0}},
              {{0,0,0,0},{0,0,0,0},{1,1,1,0},{1,0,0,0}},
              {{0,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,0,0}} },
    /* J */ { {{0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,1,0},{0,1,0,0},{0,1,0,0}},
              {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,0,1,0}},
              {{0,0,0,0},{0,1,0,0},{0,1,0,0},{1,1,0,0}} },
    /* S */ { {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
              {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,0,1,0}},
              {{0,0,0,0},{0,0,0,0},{0,1,1,0},{1,1,0,0}},
              {{0,0,0,0},{1,0,0,0},{1,1,0,0},{0,1,0,0}} },
    /* Z */ { {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
              {{0,0,0,0},{0,0,1,0},{0,1,1,0},{0,1,0,0}},
              {{0,0,0,0},{0,0,0,0},{1,1,0,0},{0,1,1,0}},
              {{0,0,0,0},{0,1,0,0},{1,1,0,0},{1,0,0,0}} }
};

/* ������ɫ����ɫ�� */
static const uint16_t shapeColors[7] = {CYAN, YELLOW, MAGENTA, WHITE, BLUE, GREEN, RED};

/* ==================== ��Ϸ���ݽṹ ==================== */
typedef struct {
    int type;      /* 0-6 */
    int x, y;      /* ��ǰ���ϽǸ������꣨�������Ϸ�壩 */
    int rotation;  /* 0-3 */
} Piece;

static int board[BOARD_HEIGHT][BOARD_WIDTH] = {0};   /* ��Ϸ�� */
static Piece current, next;                           /* ��ǰ�����һ�� */
static int score = 0;                                 /* ���� */
static int gameOver = 0;                              /* ��Ϸ������־ */

/* ��Ϸ����߽磨�ɺ�ֱ�Ӽ��㣩 */
#define BOARD_RIGHT  (BOARD_LEFT + BOARD_WIDTH * GRID_SIZE - 1)
#define BOARD_BOTTOM (BOARD_TOP + BOARD_HEIGHT * GRID_SIZE - 1)

/* ������ť���� */
typedef struct {
    uint16_t x1, y1, x2, y2;  /* �������� */
    int action;                /* �������룺0��1�ң�2�£�3��ת��4���� */
} Button;

static Button buttons[5];      /* �ĸ����Ƽ� + ���ü������Ͻǣ� */
static int lastTouchButton = -1;

/* ==================== �������� ==================== */
static void drawBoard(void);
static void drawBlock(int col, int row, uint16_t baseColor);
static void clearBlock(int col, int row);
static void drawPiece(const Piece *p);
static void clearPiece(const Piece *p);
static void drawInfo(void);
static void drawButtons(void);
static void drawArrow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, int direction, uint16_t color);
static void drawRotateSymbol(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
static int checkCollision(const Piece *p);
static void mergePiece(void);
static void spawnPiece(void);
static void movePiece(int dx, int dy);
static void rotatePiece(void);
static void resetGame(void);
static void handleTouch(void);

/* ==================== ���ߺ��� ==================== */
static const char (*getShape(const Piece *p))[4] {
    return shapes[p->type][p->rotation];
}

/**
 * @brief   �������巽�飨���߹���Ӱ��
 */
static void drawBlock(int col, int row, uint16_t baseColor) {
    uint16_t x = BOARD_LEFT + col * GRID_SIZE;
    uint16_t y = BOARD_TOP + row * GRID_SIZE;
    uint16_t xe = x + GRID_SIZE - 1;
    uint16_t ye = y + GRID_SIZE - 1;

    /* ����ɫ */
    lcd_fill(x, y, xe, ye, baseColor);

    /* ��������Ч�������������㹻��ʱ�� */
    if (GRID_SIZE >= 10) {
        /* �߹⣨���Ͻǣ� */
        lcd_fill(x, y, x + GRID_SIZE/4, y + GRID_SIZE/4, COLOR_HIGHLIGHT);
        lcd_fill(x, y, x + GRID_SIZE/2, y + 1, COLOR_HIGHLIGHT);   /* ϸ�� */
        lcd_fill(x, y + 1, x + 1, y + GRID_SIZE/2, COLOR_HIGHLIGHT);
        /* ��Ӱ�����½ǣ� */
        lcd_fill(xe - GRID_SIZE/4, ye - GRID_SIZE/4, xe, ye, COLOR_SHADOW);
        lcd_fill(xe - GRID_SIZE/2, ye, xe, ye, COLOR_SHADOW);
        lcd_fill(xe, ye - GRID_SIZE/2, xe, ye, COLOR_SHADOW);
    }
}

static void clearBlock(int col, int row) {
    uint16_t x = BOARD_LEFT + col * GRID_SIZE;
    uint16_t y = BOARD_TOP + row * GRID_SIZE;
    lcd_fill(x, y, x + GRID_SIZE - 1, y + GRID_SIZE - 1, COLOR_BG);
}

/* ==================== ��Ϸ����� ==================== */
static void drawBoard(void) {
    /* �����Ϸ���򣨱���ɫ�� */
    lcd_fill(BOARD_LEFT, BOARD_TOP, BOARD_RIGHT, BOARD_BOTTOM, COLOR_BG);

    /* �������з��� */
    for (int row = 0; row < BOARD_HEIGHT; row++) {
        for (int col = 0; col < BOARD_WIDTH; col++) {
            if (board[row][col] != 0) {
                drawBlock(col, row, shapeColors[board[row][col] - 1]);
            }
        }
    }

    /* ������Ϸ������߿򣨷�ֹ�����鸲�ǣ� */
    lcd_draw_rectangle(BOARD_LEFT - BORDER_WIDTH, BOARD_TOP - BORDER_WIDTH,
                       BOARD_RIGHT + BORDER_WIDTH, BOARD_BOTTOM + BORDER_WIDTH,
                       COLOR_BORDER);
}

/* ==================== ������� ==================== */
static void drawPiece(const Piece *p) {
    const char (*shape)[4] = getShape(p);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape[r][c]) {
                int col = p->x + c;
                int row = p->y + r;
                if (row >= 0 && row < BOARD_HEIGHT && col >= 0 && col < BOARD_WIDTH) {
                    drawBlock(col, row, shapeColors[p->type]);
                }
            }
        }
    }
}

static void clearPiece(const Piece *p) {
    const char (*shape)[4] = getShape(p);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape[r][c]) {
                int col = p->x + c;
                int row = p->y + r;
                if (row >= 0 && row < BOARD_HEIGHT && col >= 0 && col < BOARD_WIDTH) {
                    clearBlock(col, row);
                }
            }
        }
    }
}

/* ==================== ��Ϸ�߼� ==================== */
static int checkCollision(const Piece *p) {
    const char (*shape)[4] = getShape(p);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape[r][c]) {
                int col = p->x + c;
                int row = p->y + r;
                if (col < 0 || col >= BOARD_WIDTH || row >= BOARD_HEIGHT) return 1;
                if (row >= 0 && board[row][col] != 0) return 1;
            }
        }
    }
    return 0;
}

static void mergePiece(void) {
    const char (*shape)[4] = getShape(&current);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (shape[r][c]) {
                int col = current.x + c;
                int row = current.y + r;
                if (row >= 0 && row < BOARD_HEIGHT && col >= 0 && col < BOARD_WIDTH) {
                    board[row][col] = current.type + 1;
                }
            }
        }
    }
    /* �������� */
    int lines = 0;
    for (int row = BOARD_HEIGHT - 1; row >= 0; ) {
        int full = 1;
        for (int col = 0; col < BOARD_WIDTH; col++) {
            if (board[row][col] == 0) { full = 0; break; }
        }
        if (full) {
            for (int r = row; r > 0; r--) {
                memcpy(board[r], board[r-1], BOARD_WIDTH * sizeof(int));
            }
            memset(board[0], 0, BOARD_WIDTH * sizeof(int));
            lines++;
        } else {
            row--;
        }
    }
    if (lines == 1) score += 100;
    else if (lines == 2) score += 300;
    else if (lines == 3) score += 500;
    else if (lines >= 4) score += 800;

    drawBoard();
}

static void spawnPiece(void) {
    current = next;
    current.x = BOARD_WIDTH / 2 - 2;
    current.y = 0;
    current.rotation = 0;
    next.type = rand() % 7;
    next.rotation = 0;
    if (checkCollision(&current)) gameOver = 1;
}

static void movePiece(int dx, int dy) {
    Piece tmp = current;
    tmp.x += dx;
    tmp.y += dy;
    if (!checkCollision(&tmp)) {
        clearPiece(&current);
        current = tmp;
        drawPiece(&current);
        drawInfo();
    }
}

static void rotatePiece(void) {
    Piece tmp = current;
    tmp.rotation = (tmp.rotation + 1) % 4;
    if (!checkCollision(&tmp)) {
        clearPiece(&current);
        current = tmp;
        drawPiece(&current);
        drawInfo();
    }
}

static void resetGame(void) {
    memset(board, 0, sizeof(board));
    score = 0;
    gameOver = 0;
    next.type = rand() % 7;
    spawnPiece();
    lcd_clear(COLOR_BG);
    drawBoard();
    drawPiece(&current);
    drawInfo();
    drawButtons();
}

/* ==================== ����Ԫ�ػ��� ==================== */
static void drawInfo(void) {
    int infoX = BOARD_RIGHT + 20;
    int infoY = BOARD_TOP + 16;
    char buf[20];

    sprintf(buf, "%d", score);
    lcd_show_string(infoX, infoY, 100, 16, 16, "Score:", COLOR_SCORE);
lcd_show_string(infoX, infoY+16, 100, 16, 16, buf, COLOR_SCORE);

    lcd_show_string(infoX, infoY + 30, 100, 16, 16, "Next:", COLOR_SCORE);

    int previewSize = GRID_SIZE / 2;
    if (previewSize < 6) previewSize = 8;
    const char (*shape)[4] = shapes[next.type][0];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int x = infoX + c * previewSize;
            int y = infoY + 50 + r * previewSize;
            if (shape[r][c]) {
                lcd_fill(x, y, x + previewSize - 1, y + previewSize - 1, shapeColors[next.type]);
            } else {
                lcd_fill(x, y, x + previewSize - 1, y + previewSize - 1, COLOR_BG);
            }
        }
    }
}

/**
 * @brief   ���Ƽ�ͷ������0��1�ң�2�£�
 */
static void drawArrow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, int direction, uint16_t color) {
    uint16_t cx = (x1 + x2) / 2;
    uint16_t cy = (y1 + y2) / 2;
    int size = 12;  /* ��ͷ��С */

    if (direction == 0) {  /* ���ͷ */
        lcd_draw_line(cx, cy, cx - size, cy, color);
        lcd_draw_line(cx - size, cy, cx - size + 4, cy - 4, color);
        lcd_draw_line(cx - size, cy, cx - size + 4, cy + 4, color);
    } else if (direction == 1) {  /* �Ҽ�ͷ */
        lcd_draw_line(cx, cy, cx + size, cy, color);
        lcd_draw_line(cx + size, cy, cx + size - 4, cy - 4, color);
        lcd_draw_line(cx + size, cy, cx + size - 4, cy + 4, color);
    } else if (direction == 2) {  /* �¼�ͷ */
        lcd_draw_line(cx, cy, cx, cy + size, color);
        lcd_draw_line(cx, cy + size, cx - 4, cy + size - 4, color);
        lcd_draw_line(cx, cy + size, cx + 4, cy + size - 4, color);
    }
}

/**
 * @brief   ������ת���ţ�Բ+С��ͷ��
 */
static void drawRotateSymbol(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    uint16_t cx = (x1 + x2) / 2;
    uint16_t cy = (y1 + y2) / 2;
    int r = 8;  /* �뾶 */

    lcd_draw_circle(cx, cy, r, color);

    /* ��Բ�����Ϸ���һ��С��ͷ��ʾ˳ʱ�� */
    int arrowX = cx + r - 2;
    int arrowY = cy - r + 2;
    lcd_draw_line(arrowX, arrowY, arrowX + 6, arrowY - 4, color);
    lcd_draw_line(arrowX + 6, arrowY - 4, arrowX + 2, arrowY - 4, color);
    lcd_draw_line(arrowX + 6, arrowY - 4, arrowX + 4, arrowY - 2, color);
}

/**
 * @brief   ���Ƶײ���ť������ͷͼ�Σ������ֱ�ǩ��
 */
static void drawButtons(void) {
    int w = lcddev.width;
    int h = lcddev.height;
    int btnW = w / 4;
    int btnH = BUTTON_AREA_H;

    /* �����ť���� */
    lcd_fill(0, h - btnH, w - 1, h - 1, COLOR_BUTTON_BG);

    /* �����ĸ����ư�ť����ͼ�Σ������֣� */
    buttons[0].x1 = 0;               buttons[0].y1 = h - btnH; buttons[0].x2 = btnW - 1;      buttons[0].y2 = h - 1; buttons[0].action = 0;
    buttons[1].x1 = btnW;             buttons[1].y1 = h - btnH; buttons[1].x2 = btnW*2 - 1;    buttons[1].y2 = h - 1; buttons[1].action = 1;
    buttons[2].x1 = btnW*2;           buttons[2].y1 = h - btnH; buttons[2].x2 = btnW*3 - 1;    buttons[2].y2 = h - 1; buttons[2].action = 2;
    buttons[3].x1 = btnW*3;           buttons[3].y1 = h - btnH; buttons[3].x2 = w - 1;          buttons[3].y2 = h - 1; buttons[3].action = 3;
    /* ���ð�ť����������RST�� */
    buttons[4].x1 = w - 60;           buttons[4].y1 = 0;         buttons[4].x2 = w - 1;         buttons[4].y2 = 30;    buttons[4].action = 4;

    /* ����ÿ����ť */
    for (int i = 0; i < 5; i++) {
        lcd_draw_rectangle(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2, COLOR_BUTTON_BORDER);

        if (i < 4) {
            /* ���Ƽ�ͷͼ�Σ����ҡ��¡���ת�� */
            if (i == 0) drawArrow(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2, 0, COLOR_BUTTON_TXT);
            else if (i == 1) drawArrow(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2, 1, COLOR_BUTTON_TXT);
            else if (i == 2) drawArrow(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2, 2, COLOR_BUTTON_TXT);
            else if (i == 3) drawRotateSymbol(buttons[i].x1, buttons[i].y1, buttons[i].x2, buttons[i].y2, COLOR_BUTTON_TXT);
        } else {
            /* ���ð�ť��ʾ���� RST */
            int txtX = (buttons[i].x1 + buttons[i].x2 - 8 * 3) / 2;  /* "RST" 3�ַ� */
            int txtY = (buttons[i].y1 + buttons[i].y2 - 16) / 2;
            lcd_show_string(txtX, txtY, 60, 16, 16, "RST", COLOR_BUTTON_TXT);
        }
    }
}

/* ==================== �������� ==================== */
static void handleTouch(void) {
    static uint32_t lastActionTime = 0;
    uint32_t now = HAL_GetTick();

    tp_dev.scan(0);

    if (tp_dev.sta & TP_PRES_DOWN) {
        uint16_t tx = tp_dev.x[0];
        uint16_t ty = tp_dev.y[0];
        int btn = -1;

        for (int i = 0; i < 5; i++) {
            if (tx >= buttons[i].x1 && tx <= buttons[i].x2 && ty >= buttons[i].y1 && ty <= buttons[i].y2) {
                btn = i;
                break;
            }
        }

        if (btn != -1 && btn != lastTouchButton && (now - lastActionTime > 100)) {
            lastActionTime = now;
            if (gameOver) {
                if (btn == 4) resetGame();
            } else {
                switch (btn) {
                    case 0: movePiece(-1, 0); break;
                    case 1: movePiece(1, 0); break;
                    case 2: movePiece(0, 1); break;
                    case 3: rotatePiece(); break;
                    case 4: resetGame(); break;
                }
            }
        }
        lastTouchButton = btn;
    } else {
        lastTouchButton = -1;
    }
}

/* ==================== ��Ϸ��ʼ�� ==================== */
void tetris_init(void) {
    
    lcd_show_string(30, 50, 200, 16, 16, "STM32", RED);
    lcd_show_string(30, 70, 200, 16, 16, "TETRIS FIXED", RED);
    lcd_show_string(30, 90, 200, 16, 16, "Touch to Start", RED);
    delay_ms(1500);
    
    srand((unsigned int)HAL_GetTick());

    next.type = rand() % 7;
    spawnPiece();

    lcd_clear(COLOR_BG);
    drawBoard();
    drawPiece(&current);
    drawInfo();
    drawButtons();
}

/* ==================== ��Ϸ��ѭ�� ==================== */
void tetris_loop(void) {
    uint32_t lastFallTime = HAL_GetTick();

    while (1) {
        handleTouch();

        uint32_t now = HAL_GetTick();
        if (!gameOver && (now - lastFallTime >= FALL_INTERVAL)) {
            lastFallTime = now;
            Piece tmp = current;
            tmp.y++;
            if (!checkCollision(&tmp)) {
                clearPiece(&current);
                current.y++;
                drawPiece(&current);
            } else {
                mergePiece();
                spawnPiece();
                drawBoard();
                drawPiece(&current);
            }
            drawInfo();
        }

        if (gameOver) {
            int msgY = BOARD_TOP + BOARD_HEIGHT * GRID_SIZE / 2;
            lcd_show_string(BOARD_LEFT, msgY, 200, 16, 16, "GAME OVER", RED);
        }

        delay_ms(10);
    }
}


    
