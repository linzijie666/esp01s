/**
 * @file    pwm_led.h
 * @brief   PWM LED 硬件抽象层 — 仅暴露接口，隐藏寄存器操作
 *
 *          依赖: config.h
 *          用法: pwm_led_init() → 根据需要调用状态切换函数
 */

#ifndef PWM_LED_H
#define PWM_LED_H

#include <stdint.h>

/* LED 工作模式 */
typedef enum {
    LED_MODE_BREATHING = 0,     /* 呼吸模式 (渐亮渐暗) */
    LED_MODE_STEADY    = 1      /* 常亮模式 (固定亮度) */
} led_mode_t;

/* ================================================================
 *  对外接口
 * ================================================================ */

/**
 * @brief  初始化 PWM，启动呼吸定时器
 * @note   在 user_init() 中最先调用
 */
void pwm_led_init(void);

/**
 * @brief  开关 LED
 * @param  on  1=开, 0=关
 */
void pwm_led_set_power(uint8_t on);

/**
 * @brief  设置目标亮度 (百分比)
 * @param  percent  0~100
 */
void pwm_led_set_brightness(uint8_t percent);

/**
 * @brief  设置 LED 模式
 * @param  mode  LED_MODE_BREATHING 或 LED_MODE_STEADY
 */
void pwm_led_set_mode(led_mode_t mode);

/**
 * @brief  设置呼吸速度档位
 * @param  level  1~5 (1 最慢, 5 最快)
 */
void pwm_led_set_speed(uint8_t level);

/**
 * @brief  获取当前模式
 */
led_mode_t pwm_led_get_mode(void);

/**
 * @brief  获取当前占空比 (0~1023)
 */
uint32_t pwm_led_get_duty(void);

/**
 * @brief  获取当前开关状态
 */
uint8_t pwm_led_get_power(void);

/**
 * @brief  获取当前亮度百分比 (0~100)
 */
uint8_t pwm_led_get_brightness(void);

/**
 * @brief  获取当前呼吸速度档位
 */
uint8_t pwm_led_get_speed(void);

#endif /* PWM_LED_H */
