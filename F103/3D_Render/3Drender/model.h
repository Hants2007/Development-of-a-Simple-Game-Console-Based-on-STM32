#ifndef MODEL_H
#define MODEL_H

#include "vec3.h"

#define CUBE_TRI_COUNT 12
#define MODEL_TRI_COUNT 20
#define BALL_TRI_COUNT 224
#define MESHt_TRI_COUNT 44
#define NAI_TRI_COUNT 456
// 三角形图元结构体
typedef struct {
    vec3 p[3];
} triangle;


extern vec3 cube_vertices[];
extern triangle cube_tris[];
extern triangle ball_tris[];
extern triangle nai_tris[];
#endif
