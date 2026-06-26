#include "render.h"
#include <math.h>

int project(vec3 p, int* sx, int* sy)
{
    if (p.z < NEAR)
        return 0;

    float fov_rad = FOV * PI / 180.0f;
    float fov_factor = tanf(fov_rad / 2.0f);

    float x = p.x / (p.z * fov_factor);
    float y = p.y / (p.z * fov_factor);

    *sx = (int)(SCREEN_W / 2 + x * (SCREEN_W / 2));
    *sy = (int)(SCREEN_H / 2 - y * (SCREEN_W / 2));

    if (*sx < 0 || *sx >= SCREEN_W || *sy < 0 || *sy >= SCREEN_H)
        return 0;
    return 1;
}


static int is_backface_correct(vec3 cp0, vec3 cp1, vec3 cp2)
{
    // 直接使用相机空间顶点计算，无坐标偏移误差
    vec3 e1 = vec3_sub(cp1, cp0);
    vec3 e2 = vec3_sub(cp2, cp0);
    vec3 normal = vec3_cross(e1, e2);

    // 视线方向：从三角面指向相机原点(0,0,0)
    float dot = vec3_dot(normal, cp0);
    return dot < 0.0f;
}

void draw_triangle(triangle tri, MeshTransform mesh, Camera cam)
{
    // 1. 局部顶点 -> 世界空间
    vec3 wp0 = mesh_transform_vertex(tri.p[0], mesh);
    vec3 wp1 = mesh_transform_vertex(tri.p[1], mesh);
    vec3 wp2 = mesh_transform_vertex(tri.p[2], mesh);

    // 2. 世界空间 -> 相机空间（核心：剔除必须在这一步之后）
    vec3 cp0 = world_to_camera(wp0, cam);
    vec3 cp1 = world_to_camera(wp1, cam);
    vec3 cp2 = world_to_camera(wp2, cam);

    // 3. 【正确时机】相机空间背面剔除
    if(is_backface_correct(cp0, cp1, cp2))
        return;

    // 4. 透视投影+屏幕裁剪
    int x0, y0, x1, y1, x2, y2;
    if (!project(cp0, &x0, &y0)) return;
    if (!project(cp1, &x1, &y1)) return;
    if (!project(cp2, &x2, &y2)) return;

    // 5. 绘制线框
    OLED_DrawLine(x0, y0, x1, y1);
    OLED_DrawLine(x1, y1, x2, y2);
    OLED_DrawLine(x2, y2, x0, y0);
}

void render_scene(MeshTransform mesh, Camera cam)
{
    OLED_Clear();
    for (int i = 0; i < CUBE_TRI_COUNT; i++)
    {
        draw_triangle(cube_tris[i], mesh, cam);
    }
    OLED_Update();
}

void render_mesh_list(MeshObj* list, uint8_t obj_cnt, Camera cam)
{
    OLED_Clear();
    for(uint8_t m = 0; m < obj_cnt; m++)
    {
        MeshObj* obj = &list[m];
        for(uint8_t t = 0; t < obj->tri_count; t++)
        {
            draw_triangle(obj->tris[t], obj->trans, cam);
        }
    }
    OLED_Update();
}
