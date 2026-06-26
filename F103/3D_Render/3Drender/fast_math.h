#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <stdint.h>

// 快速查表三角函数，无math库依赖
float fast_cos(int angle);
float fast_sin(int angle);

// 新增：高度适配俯仰角查表补偿
// 输入相机Y高度，输出适配的pitch俯仰角
float fast_pitch(float cam_y);

#endif
