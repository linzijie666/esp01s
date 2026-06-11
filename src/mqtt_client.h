/**
 * @file    mqtt_client.h
 * @brief   MQTT 客户端 — 连接 / 订阅 / 发布 / 心跳 / 重连
 *
 *          不关心 payload 语义，只负责 MQTT 协议传输
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>

/* ================================================================
 *  对外接口
 * ================================================================ */

/**
 * @brief  初始化 MQTT (注册 TCP 数据结构, 不发起连接)
 */
void mqtt_client_init(void);

/**
 * @brief  尝试连接 MQTT Broker (需 WiFi 已连上)
 */
void mqtt_client_connect(void);

/**
 * @brief  查询是否已连接到 Broker
 */
uint8_t mqtt_client_is_connected(void);

/**
 * @brief  发布消息到指定 topic
 * @param  topic   目标 topic
 * @param  payload 消息体 (字符串)
 */
void mqtt_client_publish(const char *topic, const char *payload);

/**
 * @brief  订阅指定 topic
 */
void mqtt_client_subscribe(const char *topic);

/**
 * @brief  发送 MQTT PINGREQ
 */
void mqtt_client_ping(void);

/**
 * @brief  注册收到 publish 消息的回调
 * @param  cb  void cb(const char *topic, const char *payload)
 */
void mqtt_client_on_publish(void (*cb)(const char *topic, const char *payload));

/**
 * @brief  注册 Broker 连接成功的回调 (触发后即可 publish/subscribe)
 * @param  cb  void cb(void)
 */
void mqtt_client_on_connected(void (*cb)(void));

#endif /* MQTT_CLIENT_H */
