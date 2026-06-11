/**
 * @file    wifi_sta.c
 * @brief   WiFi Station 模式实现
 *
 *          封装 ESP8266 wifi_station 相关 API。
 *          上层只需调用 init() + 注册 on_connected 回调。
 */

#include "wifi_sta.h"
#include "config.h"

#include "esp_common.h"
#include "espressif/esp_wifi.h"
#include "espressif/esp_sta.h"

#include <string.h>

/* ================================================================
 *  模块内部状态
 * ================================================================ */
static uint8_t connected     = 0;
static void  (*on_connected)(void) = NULL;   /* 外部注册的回调 */

/* ================================================================
 *  内部回调
 * ================================================================ */

static void ICACHE_FLASH_ATTR wifi_event_handler(System_Event_t *evt)
{
    switch (evt->event_id) {

    case EVENT_STAMODE_CONNECTED:
        os_printf("[WiFi] Connected\r\n");
        break;

    case EVENT_STAMODE_GOT_IP: {
        struct ip_info info;
        wifi_get_ip_info(STATION_IF, &info);
        os_printf("[WiFi] Got IP: " IPSTR "\r\n", IP2STR(&info.ip));
        connected = 1;

        if (on_connected) {
            on_connected();   /* 通知上层: 可以发起 MQTT 连接了 */
        }
        break;
    }

    case EVENT_STAMODE_DISCONNECTED:
        os_printf("[WiFi] Disconnected, reconnecting...\r\n");
        connected = 0;
        wifi_station_connect();
        break;

    default:
        break;
    }
}

/* ================================================================
 *  对外接口
 * ================================================================ */

void wifi_sta_init(void)
{
    wifi_set_opmode(STATION_MODE);
    wifi_set_event_handler_cb(wifi_event_handler);

    struct station_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy((char *)cfg.ssid,     WIFI_SSID);
    strcpy((char *)cfg.password, WIFI_PASS);

    wifi_station_set_config(&cfg);

    os_printf("[WiFi] Connecting to: %s ...\r\n", WIFI_SSID);
}

uint8_t wifi_sta_is_connected(void)
{
    return connected;
}

void wifi_sta_on_connected(void (*cb)(void))
{
    on_connected = cb;
}
