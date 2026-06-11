/**
 * @file    main.c
 * @brief   ESP-01S MQTT 呼吸灯 — 程序入口
 *
 *          架构 (自底向上):
 *          ┌─────────────────────────────────────┐
 *          │  main.c        ← 组装 + 全局定时器    │
 *          │  led_controller ← 业务编排层          │
 *          │  pwm_led        ← 硬件抽象           │
 *          │  mqtt_client    ← 传输层             │
 *          │  wifi_sta       ← 网络层             │
 *          └─────────────────────────────────────┘
 *
 *          依赖方向: main → controller → {pwm_led, mqtt_client → wifi_sta}
 *          每层只依赖下一层，无循环依赖。
 */

#include "config.h"
#include "pwm_led.h"
#include "wifi_sta.h"
#include "mqtt_client.h"
#include "led_controller.h"

#include "esp_common.h"
#include "uart.h"

/* ================================================================
 *  全局定时器 — MQTT 心跳 + 断线重连
 * ================================================================ */

static os_timer_t glue_timer;

/**
 * @brief  定时执行: MQTT Ping / WiFi 重连 / MQTT 重连
 *
 *          这些 "粘合逻辑" 无法归属到任何单一模块，
 *          只需在这里写 3 行调度代码即可。
 */
static void ICACHE_FLASH_ATTR on_glue_timer(void *arg)
{
    /* 1. 心跳 */
    mqtt_client_ping();

    /* 2. WiFi 断开 → 重连 */
    if (!wifi_sta_is_connected()) {
        wifi_station_connect();
    }

    /* 3. WiFi 在线但 MQTT 断开 → 重连 Broker */
    if (wifi_sta_is_connected() && !mqtt_client_is_connected()) {
        mqtt_client_connect();
    }
}

/* ================================================================
 *  Flash RF 校准扇区 (SDK 强制要求)
 * ================================================================ */

uint32_t ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:       return 128 - 5;
        case FLASH_SIZE_8M_MAP_512_512:       return 256 - 5;
        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:    return 512 - 5;
        default:                              return 0;
    }
}

/* ================================================================
 *  入口
 * ================================================================ */

void user_init(void)
{
    /* 串口波特率修正 (默认 74880 → 115200) */
    UART_SetBaudrate(UART0, BIT_RATE_115200);

    os_printf("\r\n========================================\r\n");
    os_printf("  ESP-01S MQTT Breathing LED v2\r\n");
    os_printf("  Broker: %s:%d\r\n", MQTT_BROKER, MQTT_PORT);
    os_printf("========================================\r\n\r\n");

    /* ---- 第 1 层: 硬件初始化 ---- */
    pwm_led_init();

    /* ---- 第 2 层: 传输初始化 (先 init 再注册 on_connected) ---- */
    mqtt_client_init();

    /* WiFi 连接成功 → 自动发起 MQTT 连接 */
    wifi_sta_on_connected(mqtt_client_connect);

    wifi_sta_init();

    /* ---- 第 3 层: 业务初始化 ---- */
    led_controller_init();

    /* ---- 粘合层: MQTT 心跳 + 重连, 每 30 秒 ---- */
    os_timer_disarm(&glue_timer);
    os_timer_setfn(&glue_timer, (os_timer_func_t *)on_glue_timer, NULL);
    os_timer_arm(&glue_timer, MQTT_PING_MS, 1);

    os_printf("[Main] System ready.\r\n");
}
