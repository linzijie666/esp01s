/**
 * @file    config.h
 * @brief   全局配置常量 — 所有可调参数集中于此
 * @note    修改 WiFi / MQTT / PWM 参数只需改这一个文件
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ================================================================
 *  WiFi 配置
 * ================================================================ */
#define WIFI_SSID       "TP-LINK_CB33"      /* 路由器 SSID */
#define WIFI_PASS       "88888888"          /* 路由器密码 */

/* ================================================================
 *  MQTT 配置
 * ================================================================ */
#define MQTT_BROKER     "broker.emqx.io"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID  "ESP01S_LED_26366C"
#define MQTT_KEEPALIVE  60                  /* 秒 */
#define MQTT_PING_MS    30000               /* Ping 间隔 (ms) */

/* MQTT Topic */
#define TOPIC_CONTROL   "esp01s/led/control"
#define TOPIC_STATUS    "esp01s/led/status"

/* ================================================================
 *  PWM / LED 配置
 * ================================================================ */
#define PWM_PERIOD_US   1000                /* PWM 周期 (us), = 1KHz */
#define PWM_CHANNEL_NUM 1
#define PWM_DEPTH_MAX   1023                /* 10-bit 最大占空比 */
#define PWM_GPIO         2                  /* 使用的 GPIO 编号 */
#define PWM_TIMER_MS     10                 /* 默认呼吸定时器间隔 (ms) */

/* ================================================================
 *  呼吸灯档位 (5 档: step + interval)
 * ================================================================ */
#define BREATH_LEVELS   5                   /* 速度档位数 */
#define BREATH_DEFAULT  3                   /* 默认速度档位 (1-5) */

/* ================================================================
 *  业务配置
 * ================================================================ */
#define STATUS_INTERVAL_MS  2000            /* 状态上报间隔 (ms) */
#define MQTT_RECONNECT_MS   5000            /* MQTT 断开后重连间隔 (ms) */

/* ================================================================
 *  协议缓冲区
 * ================================================================ */
#define TX_BUF_SIZE     256
#define RX_BUF_SIZE     256
#define TOPIC_BUF_SIZE  64
#define PAYLOAD_BUF_SIZE 128

#endif /* CONFIG_H */
