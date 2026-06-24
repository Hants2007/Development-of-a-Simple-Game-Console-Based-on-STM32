/**
 * ================================================================
 * 摇杆驱动源文件 - STM32F407VGT6 双摇杆 ADC+DMA
 * ================================================================
 * 摇杆1(移动): PA0(IN0) PA1(IN1) PA2(SW)
 * 摇杆2(视角): PA3(IN3) PA4(IN4) PA5(SW)
 * ================================================================
 */

#include "joystick.h"
#include "stm32f4xx_hal.h"
#include <math.h>

/* ADC句柄 */
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* DMA缓冲区: [0]=VRX1, [1]=VRY1, [2]=VRX2, [3]=VRY2 */
static uint16_t adc_buffer[4];

/* 两个摇杆数据 */
static Joystick_Data_t joy1_data = {0};
static Joystick_Data_t joy2_data = {0};

/* 按键去抖 - 摇杆1 */
static uint8_t sw1_last = 1, sw1_debounced = 1;
static uint32_t sw1_time = 0;

/* 按键去抖 - 摇杆2 */
static uint8_t sw2_last = 1, sw2_debounced = 1;
static uint32_t sw2_time = 0;

/* ==================== GPIO初始化 ==================== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA0,PA1,PA3,PA4 -> 模拟输入(ADC) */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA2,PA5 -> 上拉输入(按键SW) */
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ==================== DMA初始化 ==================== */
static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
}

/* ==================== ADC1初始化(4通道) ==================== */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = ENABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 4;              /* 4个通道 */
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    HAL_ADC_Init(&hadc1);

    /* Rank1: PA0 - 摇杆1 VRX */
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    /* Rank2: PA1 - 摇杆1 VRY */
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = 2;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    /* Rank3: PA3 - 摇杆2 VRX */
    sConfig.Channel = ADC_CHANNEL_3;
    sConfig.Rank = 3;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    /* Rank4: PA4 - 摇杆2 VRY */
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = 4;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

/* ==================== 归一化一个摇杆 ==================== */
static void normalize_joystick(Joystick_Data_t *jd, uint16_t raw_x, uint16_t raw_y)
{
    jd->vrx_value = raw_x;
    jd->vry_value = raw_y;
    jd->x_normalized = ((float)raw_x - 2048.0f) / 2048.0f;
    jd->y_normalized = ((float)raw_y - 2048.0f) / 2048.0f;
}

/* ==================== 按键去抖 ==================== */
static uint8_t debounce(uint8_t current, uint8_t *last, uint8_t *debounced, uint32_t *time)
{
    if (current != *last) *time = HAL_GetTick();
    *last = current;
    if ((HAL_GetTick() - *time) > 20) {
        if (current != *debounced) *debounced = current;
    }
    return (*debounced == GPIO_PIN_RESET) ? 1 : 0;
}

/* ==================== 初始化 ==================== */
void Joystick_Init(void)
{
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();

    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buffer, 4);

    normalize_joystick(&joy1_data, 2048, 2048);
    normalize_joystick(&joy2_data, 2048, 2048);
}

/* ==================== 读取双摇杆 ==================== */
void Joystick_Read(Joystick_Data_t *joy1, Joystick_Data_t *joy2)
{
    /* ADC数据: [0]=VRX1, [1]=VRY1, [2]=VRX2, [3]=VRY2 */
    normalize_joystick(&joy1_data, adc_buffer[0], adc_buffer[1]);
    normalize_joystick(&joy2_data, adc_buffer[2], adc_buffer[3]);

    /* 按键去抖 */
    joy1_data.sw_pressed = debounce(
        HAL_GPIO_ReadPin(JOY1_SW_PORT, JOY1_SW_PIN),
        &sw1_last, &sw1_debounced, &sw1_time);
    joy2_data.sw_pressed = debounce(
        HAL_GPIO_ReadPin(JOY2_SW_PORT, JOY2_SW_PIN),
        &sw2_last, &sw2_debounced, &sw2_time);

    if (joy1) *joy1 = joy1_data;
    if (joy2) *joy2 = joy2_data;
}
