#ifndef RENDER_H
#define RENDER_H

#include "vec3.h"
#include "transform.h"
#include "model.h"
#include "mesh_list.h"
#include "oled.h"

// 渲染全局参数
#define FOV     120.0f
#define NEAR    1.0f
#define FAR     100.0f

// 透视投影：3D转2D屏幕坐标
int project(vec3 p, int* sx, int* sy);

// 背面剔除判断
int is_backface(triangle tri, Camera cam);

// 绘制单个三角形线框
void draw_triangle(triangle tri, MeshTransform mesh, Camera cam);

// 单物体场景渲染
void render_scene(MeshTransform mesh, Camera cam);

// 多物体批量渲染（支持遮挡）
void render_mesh_list(MeshObj* list, uint8_t obj_cnt, Camera cam);

#endif

