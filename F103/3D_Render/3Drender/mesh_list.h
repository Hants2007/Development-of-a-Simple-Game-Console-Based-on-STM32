#ifndef MESH_LIST_H
#define MESH_LIST_H

#include "transform.h"
#include "model.h"

// 网格物体实例（支持多物体）
typedef struct {
    MeshTransform trans;
    uint8_t tri_count;
    triangle* tris;
} MeshObj;

// 计算物体到相机距离（平方距离，无开根）
float mesh_get_cam_dist(MeshObj* obj, Camera cam);

// 冒泡排序：画家算法 远->近排序
void mesh_sort_by_depth(MeshObj* list, uint8_t obj_cnt, Camera cam);

#endif
