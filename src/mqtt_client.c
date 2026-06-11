/**
 * @file    mqtt_client.c
 * @brief   MQTT 客户端实现 — 基于 ESP8266 espconn TCP 原始 socket
 *
 *          职责: TCP 连接 / MQTT 协议编解码 / 心跳 / 重连
 *          不关心 payload 内容 (由上层回调分发)
 */

#include "mqtt_client.h"
#include "wifi_sta.h"
#include "config.h"

#include "esp_common.h"
#include "espressif/espconn.h"
#include "espressif/esp_wifi.h"
#include "lwip/lwip/dns.h"

#include <string.h>
#include <stdio.h>

/* ================================================================
 *  MQTT 协议常量 (协议层私有)
 * ================================================================ */
enum {
    PKT_CONNECT     = 0x10,
    PKT_CONNACK     = 0x20,
    PKT_PUBLISH     = 0x30,
    PKT_SUBSCRIBE   = 0x82,
    PKT_SUBACK      = 0x90,
    PKT_PINGREQ     = 0xC0,
    PKT_PINGRESP    = 0xD0,
    PKT_DISCONNECT  = 0xE0,
};

/* 连接标志位 */
#define CONN_FLAG_CLEAN_SESSION  0x02

/* ================================================================
 *  模块内部状态
 * ================================================================ */
static struct espconn  conn;
static esp_tcp         tcp;
static uint8_t         connected  = 0;
static uint8_t         connecting = 0;   /* 防止重复发起 TCP 连接 */
static uint8_t         tx_buf[TX_BUF_SIZE];

static void (*on_publish_cb)(const char *topic, const char *payload) = NULL;
static void (*on_connected_cb)(void) = NULL;

/* ================================================================
 *  内部函�数 — MQTT 包构造
 * ================================================================ */

/**
 * @brief  用当前状态构造并发送 MQTT CONNECT 包
 */
static void send_connect(void)
{
    uint8_t *p   = tx_buf;
    uint8_t *rem = NULL;

    *p++ = PKT_CONNECT;
    rem = p++;                    /* 剩余长度稍后回填 */

    /* 协议名 "MQTT" */
    *p++ = 0x00; *p++ = 0x04;
    *p++ = 'M';  *p++ = 'Q';  *p++ = 'T';  *p++ = 'T';

    /* 协议级别 */
    *p++ = 0x04;

    /* 连接标志: Clean Session */
    *p++ = CONN_FLAG_CLEAN_SESSION;

    /* Keep Alive (秒) */
    *p++ = (uint8_t)((MQTT_KEEPALIVE >> 8) & 0xFF);
    *p++ = (uint8_t)(MQTT_KEEPALIVE & 0xFF);

    /* Client ID — 拼入芯片 MAC 后 3 字节防重名 */
    uint8_t mac[6];
    wifi_get_macaddr(STATION_IF, mac);
    char cid[32];
    uint8_t cid_len = sprintf(cid, "%s_%02X%02X%02X",
                              MQTT_CLIENT_ID, mac[3], mac[4], mac[5]);
    *p++ = 0x00;
    *p++ = cid_len;
    memcpy(p, cid, cid_len);
    p += cid_len;

    *rem = (uint8_t)(p - rem - 1);
    espconn_send(&conn, tx_buf, (uint16_t)(p - tx_buf));
}

/* ================================================================
 *  TCP 回调
 * ================================================================ */

/**
 * @brief  TCP 连接成功 → 发送 MQTT CONNECT
 */
static void ICACHE_FLASH_ATTR tcp_connected_cb(void *arg)
{
    os_printf("[MQTT] TCP connected, sending CONNECT...\r\n");
    send_connect();
}

/**
 * @brief  TCP 断开 → 标记未连接，稍后由外部定时器触发重连
 */
static void ICACHE_FLASH_ATTR tcp_disconnect_cb(void *arg, sint8 err)
{
    connected  = 0;
    connecting = 0;
    os_printf("[MQTT] Disconnected (err=%d), will retry...\r\n", err);
}

/**
 * @brief  DNS 解析完成 → 发起 TCP 连接
 */
static void ICACHE_FLASH_ATTR dns_resolved_cb(const char *name,
                                               ip_addr_t *ipaddr, void *arg)
{
    if (ipaddr == NULL) {
        os_printf("[MQTT] DNS failed for %s\r\n", name);
        connecting = 0;
        return;
    }
    os_printf("[MQTT] DNS: %s -> " IPSTR "\r\n", name, IP2STR(ipaddr));

    tcp.remote_port = MQTT_PORT;
    memcpy(tcp.remote_ip, ipaddr, 4);

    conn.type           = ESPCONN_TCP;
    conn.state          = ESPCONN_NONE;
    conn.proto.tcp      = &tcp;
    /* recv_callback 已在 mqtt_client_init() 中设置，不要覆盖 */

    espconn_regist_connectcb(&conn, tcp_connected_cb);
    espconn_regist_reconcb(&conn, tcp_disconnect_cb);

    sint8 ret = espconn_connect(&conn);
    if (ret != 0) {
        os_printf("[MQTT] TCP connect failed: %d\r\n", ret);
        connecting = 0;
        /* 清理残留连接状态，避免后续 -15 */
        espconn_disconnect(&conn);
    }
}

/* ================================================================
 *  内部函数 — 接收数据解析
 * ================================================================ */

/**
 * @brief  TCP 数据到达回调 → 解析 MQTT 帧
 */
static void ICACHE_FLASH_ATTR tcp_recv_cb(void *arg, char *pdata,
                                           unsigned short len)
{
    uint8_t *buf = (uint8_t *)pdata;
    if (len == 0) return;

    uint8_t pkt_type = buf[0] & 0xF0;

    switch (pkt_type) {

    case PKT_CONNACK:
        /* CONNACK 报文结构: buf[0]=0x20, buf[1]=剩余长度(0x02),
         * buf[2]=连接标志, buf[3]=返回码(0x00=成功) */
        if (len >= 4 && buf[3] == 0) {
            connected  = 1;
            connecting = 0;
            os_printf("[MQTT] Broker connected!\r\n");
            if (on_connected_cb) on_connected_cb();

        } else {
            connecting = 0;
            os_printf("[MQTT] CONNACK rejected, len=%d, return_code=%d, closing TCP...\r\n",
                      len, len >= 4 ? buf[3] : -1);
            espconn_disconnect(&conn);   /* 必须断开，否则下次 connect 返回 -15 */
        }
        break;

    case PKT_SUBACK:
        os_printf("[MQTT] Subscribe OK\r\n");
        break;

    case PKT_PUBLISH: {
        /* ---- 解析 topic ---- */
        uint32_t pos      = 2;
        uint16_t topic_len = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        pos += 2;

        char topic[TOPIC_BUF_SIZE];
        if (topic_len >= TOPIC_BUF_SIZE) topic_len = TOPIC_BUF_SIZE - 1;
        memcpy(topic, &buf[pos], topic_len);
        topic[topic_len] = '\0';
        pos += topic_len;

        /* 跳过 Packet Identifier (QoS > 0 时) */
        uint8_t qos = (buf[0] & 0x06) >> 1;
        if (qos > 0) pos += 2;

        /* ---- 解析 payload ---- */
        uint32_t plen = len - pos;
        char payload[PAYLOAD_BUF_SIZE];
        if (plen >= PAYLOAD_BUF_SIZE) plen = PAYLOAD_BUF_SIZE - 1;
        memcpy(payload, &buf[pos], plen);
        payload[plen] = '\0';

        os_printf("[MQTT] ← %s\r\n", topic);

        /* 交给上层回调 */
        if (on_publish_cb) {
            on_publish_cb(topic, payload);
        }
        break;
    }

    case PKT_PINGRESP:
        os_printf("[MQTT] Pong\r\n");
        break;

    default:
        break;
    }
}

/* ================================================================
 *  对外接口
 * ================================================================ */

void mqtt_client_init(void)
{
    conn.recv_callback = tcp_recv_cb;
}

void mqtt_client_connect(void)
{
    if (connecting || connected) return;
    if (!wifi_sta_is_connected()) return;
    connecting = 1;

    ip_addr_t addr;
    err_t err = espconn_gethostbyname(&conn, MQTT_BROKER, &addr,
                                      dns_resolved_cb);
    if (err == ESPCONN_OK) {
        /* DNS 缓存命中, 直接走回调 */
        dns_resolved_cb(MQTT_BROKER, &addr, NULL);
    }
}

uint8_t mqtt_client_is_connected(void)
{
    return connected;
}

void mqtt_client_publish(const char *topic, const char *payload)
{
    if (!connected) return;

    uint8_t *p   = tx_buf;
    uint8_t *rem = NULL;

    *p++ = PKT_PUBLISH;   /* QoS 0 */
    rem  = p++;

    uint8_t tlen  = (uint8_t)strlen(topic);
    uint16_t plen = (uint16_t)strlen(payload);

    *p++ = 0x00;
    *p++ = tlen;
    memcpy(p, topic, tlen);
    p += tlen;

    memcpy(p, payload, plen);
    p += plen;

    *rem = (uint8_t)(p - rem - 1);
    espconn_send(&conn, tx_buf, (uint16_t)(p - tx_buf));
}

void mqtt_client_subscribe(const char *topic)
{
    if (!connected) return;

    uint8_t *p   = tx_buf;
    uint8_t *rem = NULL;

    *p++ = PKT_SUBSCRIBE;
    rem  = p++;

    *p++ = 0x00; *p++ = 0x01;   /* Packet Identifier = 1 */

    uint8_t tlen = (uint8_t)strlen(topic);
    *p++ = 0x00;
    *p++ = tlen;
    memcpy(p, topic, tlen);
    p += tlen;

    *p++ = 0x00;   /* QoS 0 */

    *rem = (uint8_t)(p - rem - 1);
    espconn_send(&conn, tx_buf, (uint16_t)(p - tx_buf));
}

void mqtt_client_ping(void)
{
    if (!connected) return;
    tx_buf[0] = PKT_PINGREQ;
    tx_buf[1] = 0x00;
    espconn_send(&conn, tx_buf, 2);
}

void mqtt_client_on_connected(void (*cb)(void))
{
    on_connected_cb = cb;
}

void mqtt_client_on_publish(void (*cb)(const char *topic,
                                       const char *payload))
{
    on_publish_cb = cb;
}

