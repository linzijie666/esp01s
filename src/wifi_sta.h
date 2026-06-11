/**
 * @file    wifi_sta.h
 * @brief   WiFi Station 模式 — 连接 / 重连 / 状态查询
 *
 *          对外只暴露出状态和初始化函数，不暴露 espconn / wifi 底层细节
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stdint.h>

/* ================================================================
 *  对外接口
 * ================================================================ */

/**
 * @brief  初始化 WiFi Station 模式并开始连接
 * @note   调用后立即返回，连接结果通过回调异步通知
 */
void wifi_sta_init(void);

/**
 * @brief  查询 WiFi 是否已获取 IP
 * @return 1=已连接, 0=未连接
 */
uint8_t wifi_sta_is_connected(void);

/**
 * @brief  注册 WiFi 连接成功后的回调 (用于触发 MQTT 连接)
 * @param  cb  回调函数指针
 */
void wifi_sta_on_connected(void (*cb)(void));

#endif /* WIFI_STA_H */
