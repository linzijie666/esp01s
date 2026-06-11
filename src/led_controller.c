/**
 * @file    led_controller.c
 * @brief   业务逻辑层 — JSON 指令解析 + PWM / MQTT 编排
 *
 *          这是连接 "硬件层" 和 "传输层" 的桥梁。
 *          不直接操作寄存器，不拼 MQTT 报文。
 */

#include "led_controller.h"
#include "pwm_led.h"
#include "mqtt_client.h"
#include "config.h"

#include "esp_common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 *  内部辅助 — 简单 JSON 字符串提取
 *  (避免引入 cJSON 库，节省 flash — 仅 250 字节代码量)
 * ================================================================ */

/**
 * @brief  从 JSON 字符串中提取 "key":"value" 的 value 字符串
 * @return 找到返回 value 指针 (static buf), 否则 NULL
 */
static const char *json_get_str(const char *json, const char *key)
{
    static char buf[24];
    const char *cp = strstr(json, key);
    if (!cp) return NULL;

    const char *colon = strstr(cp, ":");
    if (!colon) return NULL;

    /* 跳过冒号和空白/引号 */
    const char *v = colon + 1;
    while (*v == '\"' || *v == ' ' || *v == '\t') v++;

    int i = 0;
    while (*v && *v != '\"' && *v != ',' && *v != '}' && i < 23) {
        buf[i++] = *v++;
    }
    buf[i] = '\0';
    return buf;
}

/**
 * @brief  从 JSON 中提取 "key":N 的整数值
 */
static int json_get_int(const char *json, const char *key)
{
    const char *cp = strstr(json, key);
    if (!cp) return -1;

    const char *colon = strstr(cp, ":");
    if (!colon) return -1;

    const char *n = colon + 1;
    /* 跳过冒号、空格、双引号 — 兼容 MQTTX 把数字写成字符串的情况 */
    while (*n == ':' || *n == ' ' || *n == '\"') n++;

    return atoi(n);
}

/* ================================================================
 *  内部逻辑
 * ================================================================ */

/**
 * @brief  解析 JSON 指令 → 调用 pwm_led_* 执行
 */
static void parse_and_execute(const char *json)
{
    const char *cmd = json_get_str(json, "\"cmd\"");
    if (!cmd) return;

    int val = json_get_int(json, "\"value\"");

    /* ---- 开关 ---- */
    if (strcmp(cmd, "power") == 0 && val >= 0) {
        pwm_led_set_power(val ? 1 : 0);
        os_printf("[LED] Power: %s\r\n", val ? "ON" : "OFF");
    }
    /* ---- 亮度 ---- */
    else if (strcmp(cmd, "brightness") == 0 && val >= 0 && val <= 100) {
        pwm_led_set_brightness((uint8_t)val);
        os_printf("[LED] Brightness: %d%%\r\n", val);
    }
    /* ---- 模式 ---- */
    else if (strcmp(cmd, "mode") == 0) {
        if (strstr(json, "steady")) {
            pwm_led_set_mode(LED_MODE_STEADY);
        } else if (strstr(json, "breathing")) {
            pwm_led_set_mode(LED_MODE_BREATHING);
        }
        os_printf("[LED] Mode: %s\r\n",
                  pwm_led_get_mode() ? "steady" : "breathing");
    }
    /* ---- 速度 ---- */
    else if (strcmp(cmd, "speed") == 0 && val >= 1 && val <= 5) {
        pwm_led_set_speed((uint8_t)val);
        os_printf("[LED] Speed: %d\r\n", val);
    }
}

/**
 * @brief  MQTT publish 消息回调 — 从 mqtt_client 层传入
 */
static void on_mqtt_message(const char *topic, const char *payload)
{
    if (strcmp(topic, TOPIC_CONTROL) == 0) {
        parse_and_execute(payload);
        led_controller_report_status();   /* 立即反馈 */
    }
}

/* ================================================================
 *  定时器 — 状态上报
 * ================================================================ */

static os_timer_t status_timer;

static void ICACHE_FLASH_ATTR on_status_timer(void *arg)
{
    led_controller_report_status();
}

/* ================================================================
 *  对外接口
 * ================================================================ */

/**
 * @brief  MQTT Broker 连接成功后: 订阅控制 topic + 首次上报状态
 */
static void on_mqtt_connected(void)
{
    mqtt_client_subscribe(TOPIC_CONTROL);
    led_controller_report_status();
}

void led_controller_init(void)
{
    /* 注册 MQTT 回调: 收到 control topic → 解析执行 */
    mqtt_client_on_publish(on_mqtt_message);

    /* 注册 MQTT 连接成功回调: 订阅 + 首次上报 */
    mqtt_client_on_connected(on_mqtt_connected);

    /* 启动状态上报定时器 */
    os_timer_disarm(&status_timer);
    os_timer_setfn(&status_timer, (os_timer_func_t *)on_status_timer, NULL);
    os_timer_arm(&status_timer, STATUS_INTERVAL_MS, 1);

    os_printf("[Ctrl] Controller ready\r\n");
}

void led_controller_handle_command(const char *json)
{
    parse_and_execute(json);
}

void led_controller_report_status(void)
{
    if (!mqtt_client_is_connected()) return;

    /* 当前亮度: 常亮模式用 brightness, 呼吸模式用实际 duty */
    uint32_t bri = (pwm_led_get_mode() == LED_MODE_STEADY)
                   ? pwm_led_get_brightness()
                   : (pwm_led_get_duty() * 100) / PWM_DEPTH_MAX;

    char payload[96];
    sprintf(payload,
            "{\"power\":%d,\"brightness\":%d,\"mode\":\"%s\",\"speed\":%d}",
            pwm_led_get_power(),
            (int)bri,
            pwm_led_get_mode() ? "steady" : "breathing",
            pwm_led_get_speed());

    mqtt_client_publish(TOPIC_STATUS, payload);
}
