#include "mesh_list.h"
#include "vec3.h"

float mesh_get_cam_dist(MeshObj* obj, Camera cam)
{
    vec3 center_world = mesh_transform_vertex((vec3){0,0,0}, obj->trans);
    vec3 delta = vec3_sub(center_world, cam.pos);
    return delta.x*delta.x + delta.y*delta.y + delta.z*delta.z;
}

void mesh_sort_by_depth(MeshObj* list, uint8_t obj_cnt, Camera cam)
{
    uint8_t i,j;
    for(i = 0; i < obj_cnt - 1; i++)
    {
        for(j = 0; j < obj_cnt - 1 - i; j++)
        {
            float d1 = mesh_get_cam_dist(&list[j], cam);
            float d2 = mesh_get_cam_dist(&list[j+1], cam);
            if(d1 < d2)
            {
                MeshObj tmp = list[j];
                list[j] = list[j+1];
                list[j+1] = tmp;
            }
        }
    }
}
