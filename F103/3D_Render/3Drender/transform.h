#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "vec3.h"

// 相机结构体：位置 + 水平偏航 + 垂直俯仰
typedef struct {
    vec3 pos;
    float yaw;
    float pitch;
} Camera;

// 网格物体变换参数
typedef struct {
    vec3 pos;
    vec3 rot;
    vec3 scale;
} MeshTransform;

// 顶点局部空间转世界空间
vec3 mesh_transform_vertex(vec3 local_p, MeshTransform trans);

// 世界空间转相机空间
vec3 world_to_camera(vec3 world_p, Camera cam);

// 相机控制API
void camera_move_forward(Camera* cam, float step);
void camera_move_right(Camera* cam, float step);
void camera_move_up(Camera* cam, float step);
void camera_rotate_yaw(Camera* cam, float rad_step);
void camera_rotate_pitch(Camera* cam, float rad_step);

// 网格物体控制API
void mesh_translate(MeshTransform* mesh, vec3 offset);
void mesh_rotate(MeshTransform* mesh, vec3 rad_step);
void mesh_set_scale(MeshTransform* mesh, vec3 s);

#endif
