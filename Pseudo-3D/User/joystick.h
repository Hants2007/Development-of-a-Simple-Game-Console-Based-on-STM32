/**
 * ================================================================
 * 摇杆驱动头文件 - STM32F407VGT6 (双摇杆)
 * ================================================================
 * 摇杆1(移动): PA0(VRX) PA1(VRY) PA2(SW)
 * 摇杆2(视角): PA3(VRX) PA4(VRY) PA5(SW)
 * VCC -> 3.3V, GND -> GND
 * ================================================================
 */

#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f4xx_hal.h"

/* 摇杆1引脚 - 移动 */
#define JOY1_VRX_PORT    GPIOA
#define JOY1_VRX_PIN     GPIO_PIN_0

#define JOY1_VRY_PORT    GPIOA
#define JOY1_VRY_PIN     GPIO_PIN_1

#define JOY1_SW_PORT     GPIOA
#define JOY1_SW_PIN      GPIO_PIN_2

/* 摇杆2引脚 - 视角 */
#define JOY2_VRX_PORT    GPIOA
#define JOY2_VRX_PIN     GPIO_PIN_3

#define JOY2_VRY_PORT    GPIOA
#define JOY2_VRY_PIN     GPIO_PIN_4

#define JOY2_SW_PORT     GPIOA
#define JOY2_SW_PIN      GPIO_PIN_5

/* ADC句柄 */
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

/* 摇杆数据结构体 */
typedef struct {
    uint16_t vrx_value;
    uint16_t vry_value;
    uint8_t  sw_pressed;
    float    x_normalized;
    float    y_normalized;
} Joystick_Data_t;

/* 函数声明 */
void Joystick_Init(void);
void Joystick_Read(Joystick_Data_t *joy1, Joystick_Data_t *joy2);

#endif /* __JOYSTICK_H */
