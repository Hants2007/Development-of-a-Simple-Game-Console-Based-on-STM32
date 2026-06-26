#include "transform.h"
#include <math.h>

vec3 mesh_transform_vertex(vec3 local_p, MeshTransform trans)
{
    vec3 p = vec3_mul(local_p, trans.scale);
    p = vec3_rotate_xyz(p, trans.rot);
    p = vec3_add(p, trans.pos);
    return p;
}

vec3 world_to_camera(vec3 world_p, Camera cam)
{
    vec3 p = vec3_sub(world_p, cam.pos);
    p = vec3_rotate_y(p, -cam.yaw);
    p = vec3_rotate_x(p, -cam.pitch);
    return p;
}

void camera_move_forward(Camera* cam, float step)
{
    float sin_y = sinf(cam->yaw);
    float cos_y = cosf(cam->yaw);
    cam->pos.x += sin_y * step;
    cam->pos.z += cos_y * step;
}

void camera_move_right(Camera* cam, float step)
{
    float sin_y = sinf(cam->yaw);
    float cos_y = cosf(cam->yaw);
    cam->pos.x += cos_y * step;
    cam->pos.z -= sin_y * step;
}

void camera_move_up(Camera* cam, float step)
{
    cam->pos.y += step;
}

void camera_rotate_yaw(Camera* cam, float rad_step)
{
    cam->yaw += rad_step;
}

void camera_rotate_pitch(Camera* cam, float rad_step)
{
    cam->pitch += rad_step;
    if (cam->pitch > PI / 2.0f - 0.1f)
        cam->pitch = PI / 2.0f - 0.1f;
    if (cam->pitch < -PI / 2.0f + 0.1f)
        cam->pitch = -PI / 2.0f + 0.1f;
}

void mesh_translate(MeshTransform* mesh, vec3 offset)
{
    mesh->pos = vec3_add(mesh->pos, offset);
}

void mesh_rotate(MeshTransform* mesh, vec3 rad_step)
{
    mesh->rot.x += rad_step.x;
    mesh->rot.y += rad_step.y;
    mesh->rot.z += rad_step.z;
}

void mesh_set_scale(MeshTransform* mesh, vec3 s)
{
    mesh->scale = s;
}
