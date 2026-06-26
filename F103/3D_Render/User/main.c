#include "vec3.h"
#include "transform.h"
#include "render.h"
#include "mesh_list.h"
#include "oled.h"
#include <math.h>


#define MESH_OBJ_NUM 1
MeshObj mesh_arr[MESH_OBJ_NUM] = {
    {
        .trans = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        .tri_count =BALL_TRI_COUNT,
        .tris =ball_tris
    }
};


#define CAM_X    -3.0f
#define CAM_Y    0.6f
#define CAM_Z    0.0f
#define CAM_YAW  (PI / 2.0f)
#define CAM_PITCH 0.15f

int main(void)
{
    OLED_Init();

    Camera main_cam;
		float angle=0.0f;
	float scale=0.5f;
    main_cam.pos.x = CAM_X;
    main_cam.pos.y = CAM_Y;
    main_cam.pos.z = CAM_Z;
    main_cam.yaw   = CAM_YAW;
    main_cam.pitch = CAM_PITCH;

    while (1)
    {
				angle+=0.2f;
				if(angle>6.28f) angle=0;
				scale+=0.2f;
			if(scale>1.5f) scale=0.5f;
			
				mesh_arr[0].trans.rot.y=angle;
		
        mesh_sort_by_depth(mesh_arr, MESH_OBJ_NUM, main_cam);
        render_mesh_list(mesh_arr, MESH_OBJ_NUM, main_cam);
    }
}
