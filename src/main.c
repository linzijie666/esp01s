/**
 * ESP8266 GPIO2 PWM 调光实验 (呼吸灯)
 *
 * 硬件: ESP-01S (ESP8266)
 * 引脚: GPIO2 → R6(1K) → Q1(SS8050) → LED1-24
 * 框架: ESP8266 RTOS SDK
 *
 * 功能:
 *  - 通过 PWM 驱动 GPIO2 实现 LED 呼吸灯效果
 *  - 占空比 0→1023→0 循环渐变
 */

#include "esp_common.h"
#include "espressif/pwm.h"
#include "espressif/esp8266/pin_mux_register.h"

/* ========== PWM 配置 ========== */
#define PWM_PERIOD      1000        /* PWM 周期, 单位: us (1000us = 1KHz) */
#define PWM_CHANNEL_NUM 1           /* 只用 1 个 PWM 通道 */
#define PWM_STEP        5           /* 每次变化的步长 */
#define PWM_INTERVAL_MS 10          /* 定时器间隔, 单位: ms */

static uint32 g_duty = 0;           /* 当前占空比 (0~1023) */
static int8_t g_direction = 1;      /* 1=变亮, -1=变暗 */
static os_timer_t g_pwm_timer;

/**
 * PWM 呼吸灯定时器回调
 * 每隔 PWM_INTERVAL_MS 毫秒调整一次占空比
 */
static void ICACHE_FLASH_ATTR pwm_timer_cb(void *arg)
{
    g_duty += g_direction * PWM_STEP;

    /* 到达最大亮度时反向 (变暗) */
    if (g_duty >= PWM_DEPTH) {
        g_duty = PWM_DEPTH;
        g_direction = -1;
    }
    /* 到达最暗时反向 (变亮) */
    else if (g_duty == 0) {
        g_duty = 0;
        g_direction = 1;
    }

    /* 更新占空比并生效 */
    pwm_set_duty(g_duty, 0);
    pwm_start();
}

/**
 * Flash RF 校准扇区 (SDK 必需)
 */
uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;
        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;
        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }
    return rf_cal_sec;
}

/**
 * 用户初始化入口 (ESP8266 RTOS SDK 规范)
 */
void user_init(void)
{
    os_printf("\r\n");
    os_printf("========================================\r\n");
    os_printf("  ESP8266 GPIO2 PWM 呼吸灯\r\n");
    os_printf("  硬件: ESP-01S (1MB Flash)\r\n");
    os_printf("  GPIO2 → Q1(SS8050) → LED×24\r\n");
    os_printf("  PWM: %dHz, 占空比 0~%d\r\n", 1000000 / PWM_PERIOD, PWM_DEPTH);
    os_printf("========================================\r\n\r\n");

    /* ---- PWM 引脚配置 ----
     * pin_info 格式: {复用寄存器, 功能号, GPIO编号}
     * GPIO2: PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2(0), pin=2
     */
    uint32 pin_info[PWM_CHANNEL_NUM][3] = {
        {PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2, 2}
    };

    /* 初始占空比: 0 (LED 全灭) */
    uint32 init_duty[PWM_CHANNEL_NUM] = {0};

    /* 初始化 PWM: 周期 1000us, 1 个通道 */
    pwm_init(PWM_PERIOD, init_duty, PWM_CHANNEL_NUM, pin_info);
    pwm_start();

    os_printf("[PWM] GPIO2 PWM 初始化完成\r\n");
    os_printf("[PWM] 开始呼吸灯效果...\r\n\r\n");

    /* 启动呼吸灯定时器 (周期性调节占空比) */
    os_timer_disarm(&g_pwm_timer);
    os_timer_setfn(&g_pwm_timer, (os_timer_func_t *)pwm_timer_cb, NULL);
    os_timer_arm(&g_pwm_timer, PWM_INTERVAL_MS, 1);  /* 每 10ms 重复 */
}
