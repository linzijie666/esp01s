/**
 * @file    led_controller.h
 * @brief   业务逻辑层 — 解析 JSON 指令 + 编排 PWM / MQTT 上报
 *
 *          依赖: pwm_led.h, mqtt_client.h, config.h
 *          不直接操作硬件或拼 MQTT 包
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>

/* ================================================================
 *  对外接口
 * ================================================================ */

/**
 * @brief  初始化业务层 (注册 MQTT 回调 + 启动定时上报)
 */
void led_controller_init(void);

/**
 * @brief  处理收到的控制指令 (JSON 字符串)
 * @param  json  例如: {"cmd":"power","value":1}
 */
void led_controller_handle_command(const char *json);

/**
 * @brief  上报当前状态到 MQTT
 */
void led_controller_report_status(void);

#endif /* LED_CONTROLLER_H */
