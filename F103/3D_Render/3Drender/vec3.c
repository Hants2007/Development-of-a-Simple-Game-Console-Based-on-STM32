#include "vec3.h"
#include <math.h>

vec3 vec3_add(vec3 a, vec3 b)
{
    vec3 res = {a.x + b.x, a.y + b.y, a.z + b.z};
    return res;
}

vec3 vec3_sub(vec3 a, vec3 b)
{
    vec3 res = {a.x - b.x, a.y - b.y, a.z - b.z};
    return res;
}

vec3 vec3_mul_scalar(vec3 v, float s)
{
    vec3 res = {v.x * s, v.y * s, v.z * s};
    return res;
}

vec3 vec3_mul(vec3 a, vec3 b)
{
    vec3 res = {a.x * b.x, a.y * b.y, a.z * b.z};
    return res;
}

vec3 vec3_cross(vec3 a, vec3 b)
{
    vec3 res;
    res.x = a.y * b.z - a.z * b.y;
    res.y = a.z * b.x - a.x * b.z;
    res.z = a.x * b.y - a.y * b.x;
    return res;
}

float vec3_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_rotate_x(vec3 v, float rad)
{
    float c = cosf(rad);
    float s = sinf(rad);
    vec3 out;
    out.x = v.x;
    out.y = v.y * c - v.z * s;
    out.z = v.y * s + v.z * c;
    return out;
}

vec3 vec3_rotate_y(vec3 v, float rad)
{
    float c = cosf(rad);
    float s = sinf(rad);
    vec3 out;
    out.x = v.x * c + v.z * s;
    out.y = v.y;
    out.z = -v.x * s + v.z * c;
    return out;
}

vec3 vec3_rotate_z(vec3 v, float rad)
{
    float c = cosf(rad);
    float s = sinf(rad);
    vec3 out;
    out.x = v.x * c - v.y * s;
    out.y = v.x * s + v.y * c;
    out.z = v.z;
    return out;
}

vec3 vec3_rotate_xyz(vec3 v, vec3 rot_rad)
{
    v = vec3_rotate_x(v, rot_rad.x);
    v = vec3_rotate_y(v, rot_rad.y);
    v = vec3_rotate_z(v, rot_rad.z);
    return v;
}
