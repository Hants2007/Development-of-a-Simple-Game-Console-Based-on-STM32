#ifndef VEC3_H
#define VEC3_H

#include <stdint.h>

#define PI 3.14159265f

// 三维向量结构体
typedef struct {
    float x, y, z;
} vec3;

// 向量基础运算
vec3 vec3_add(vec3 a, vec3 b);
vec3 vec3_sub(vec3 a, vec3 b);
vec3 vec3_mul_scalar(vec3 v, float s);
vec3 vec3_mul(vec3 a, vec3 b);

// 向量几何运算
vec3 vec3_cross(vec3 a, vec3 b);
float vec3_dot(vec3 a, vec3 b);

// 向量旋转变换
vec3 vec3_rotate_x(vec3 v, float rad);
vec3 vec3_rotate_y(vec3 v, float rad);
vec3 vec3_rotate_z(vec3 v, float rad);
vec3 vec3_rotate_xyz(vec3 v, vec3 rot_rad);

#endif
