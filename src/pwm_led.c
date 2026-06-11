/**
 * @file    pwm_led.c
 * @brief   PWM LED 硬件抽象层实现
 *
 *          直接操作 GPIO2 PWM 寄存器，管理呼吸灯定时器。
 *          对外只暴露行为接口，内部状态全部 static 封闭。
 */

#include "esp_common.h"
#include "pwm_led.h"
#include "config.h"

#include "espressif/pwm.h"
#include "espressif/esp8266/pin_mux_register.h"

/* ================================================================
 *  呼吸速度档位 (步长 + 定时器间隔)
 * ================================================================ */
static const uint32_t breath_steps[] = {2, 5, 10, 20, 40};
static const uint32_t breath_ms[]   = {30, 20, 15, 10, 8};

/* ================================================================
 *  模块内部状态 (外部不可见)
 * ================================================================ */
static os_timer_t  timer;
static uint32_t    duty        = 0;        /* 当前占空比 0~1023 */
static int8_t      direction   = 1;        /* 1=变亮, -1=变暗 */
static uint8_t     power       = 1;        /* 0=关, 1=开 */
static uint8_t     brightness  = 100;      /* 目标亮度 0~100 */
static uint8_t     speed_level = BREATH_DEFAULT;
static led_mode_t  mode        = LED_MODE_BREATHING;

/* ================================================================
 *  内部函数
 * ================================================================ */

/**
 * @brief  根据当前速度和亮度重新装载定时器
 */
static void timer_rearm(void)
{
    os_timer_disarm(&timer);
    if (speed_level > 0 && speed_level <= BREATH_LEVELS) {
        os_timer_arm(&timer, breath_ms[speed_level - 1], 0);
    }
}

/**
 * @brief  呼吸灯定时器回调 (10~30ms 间隔)
 */
static void ICACHE_FLASH_ATTR on_timer(void *arg)
{
    if (!power) {
        /* 关机: 占空比归零 */
        pwm_set_duty(0, 0);
        pwm_start();
        return;
    }

    if (mode == LED_MODE_BREATHING) {
        uint32_t step    = breath_steps[speed_level - 1];
        uint32_t max_duty = (uint32_t)brightness * PWM_DEPTH_MAX / 100;

        duty += (int32_t)direction * (int32_t)step;

        if (duty >= max_duty) {
            duty      = max_duty;
            direction = -1;
        } else if (duty == 0) {
            duty      = 0;
            direction = 1;
        }

        timer_rearm();  /* 根据速度档位调整下一次触发时间 */
    }
    /* 常亮模式: 占空比由 set_brightness/set_mode 直接写入，这里不动 */

    pwm_set_duty(duty, 0);
    pwm_start();
}

/* ================================================================
 *  对外接口实现
 * ================================================================ */

void pwm_led_init(void)
{
    uint32_t pin_info[1][3] = {
        {PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2, PWM_GPIO}
    };
    uint32_t init_duty[1] = {0};

    pwm_init(PWM_PERIOD_US, init_duty, 1, pin_info);
    pwm_start();

    os_timer_disarm(&timer);
    os_timer_setfn(&timer, (os_timer_func_t *)on_timer, NULL);
    os_timer_arm(&timer, breath_ms[BREATH_DEFAULT - 1], 0);

    os_printf("[PWM] GPIO%d init OK, %dHz\r\n", PWM_GPIO, 1000000 / PWM_PERIOD_US);
}

void pwm_led_set_power(uint8_t on)
{
    power = on ? 1 : 0;
    if (!power) {
        pwm_set_duty(0, 0);
        pwm_start();
    } else {
        /* 恢复亮度 */
        if (mode == LED_MODE_STEADY) {
            duty = (uint32_t)brightness * PWM_DEPTH_MAX / 100;
            pwm_set_duty(duty, 0);
            pwm_start();
        } else {
            /* 呼吸模式：从 0 开始呼吸，重启定时器 */
            duty = 0;
            direction = 1;
            pwm_set_duty(duty, 0);
            pwm_start();
            timer_rearm();
        }
    }
}

void pwm_led_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    brightness = percent;

    if (mode == LED_MODE_STEADY) {
        duty = (uint32_t)brightness * PWM_DEPTH_MAX / 100;
        /* 只有开机状态下才直接写入硬件，关机时只保存变量 */
        if (power) {
            pwm_set_duty(duty, 0);
            pwm_start();
        }
    }
    /* 呼吸模式: 不立即改变当前占空比，只限制呼吸上限 */
}

void pwm_led_set_mode(led_mode_t new_mode)
{
    mode = new_mode;
    if (mode == LED_MODE_STEADY) {
        /* 切换到常亮：立即跳到目标亮度 */
        duty = (uint32_t)brightness * PWM_DEPTH_MAX / 100;
        /* 只有开机状态下才直接写入硬件 */
        if (power) {
            pwm_set_duty(duty, 0);
            pwm_start();
        }
    } else {
        /* 切换到呼吸：重启定时器 (若已开机) */
        if (power) timer_rearm();
    }
}

void pwm_led_set_speed(uint8_t level)
{
    if (level < 1) level = 1;
    if (level > BREATH_LEVELS) level = BREATH_LEVELS;
    speed_level = level;
}

/* ---- 简单 getter ---- */
led_mode_t pwm_led_get_mode(void)       { return mode; }
uint32_t   pwm_led_get_duty(void)       { return duty; }
uint8_t    pwm_led_get_power(void)      { return power; }
uint8_t    pwm_led_get_brightness(void)  { return brightness; }
uint8_t    pwm_led_get_speed(void)       { return speed_level; }
