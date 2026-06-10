/**
 * ESP8266 WiFi 实验
 *
 * 功能:
 *  - 扫描附近 WiFi 热点
 *  - 连接到指定 AP
 *  - 监控连接状态 (RSSI 信号强度, IP 地址)
 *  - 断线自动重连
 *
 * 硬件: ESP-01S (ESP8266)
 * 框架: ESP8266 RTOS SDK (v1.5.0)
 */

#include "esp_common.h"
#include "espressif/esp_wifi.h"
#include "espressif/esp_sta.h"
#include "lwip/ip_addr.h"

/* ========== WiFi 配置（修改为你的网络）========== */
#define WIFI_SSID      "TP-LINK_CB33"  /* 替换为你的 WiFi SSID */
#define WIFI_PASSWORD  "88888888"

/* 状态机 */
typedef enum {
    WIFI_STATE_INIT,        /* 初始化 */
    WIFI_STATE_SCANNING,    /* 正在扫描 */
    WIFI_STATE_CONNECTING,  /* 正在连接 */
    WIFI_STATE_CONNECTED,   /* 已连接 */
    WIFI_STATE_FAILED,      /* 连接失败 */
} wifi_state_t;

static wifi_state_t g_wifi_state = WIFI_STATE_INIT;
static os_timer_t g_scan_timer;
static os_timer_t g_status_timer;

/**
 * Flash RF 校准扇区（SDK 必需）
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

/* ========== 扫描回调 ========== */
static void ICACHE_FLASH_ATTR scan_done_cb(void *arg, STATUS status)
{
    if (status != OK) {
        os_printf("[SCAN] 扫描失败, status=%d\r\n", status);
        g_wifi_state = WIFI_STATE_CONNECTING;
        return;
    }

    struct bss_info *bss = (struct bss_info *)arg;
    int count = 0;

    os_printf("\r\n========================================\r\n");
    os_printf("  扫描结果 (WiFi 热点列表)\r\n");
    os_printf("========================================\r\n");

    while (bss != NULL) {
        count++;
        os_printf("[%2d] %-20s  CH:%2d  RSSI:%4d dBm  Auth:%d\r\n",
                 count, bss->ssid, bss->channel, bss->rssi, bss->authmode);
        bss = STAILQ_NEXT(bss, next);
    }

    os_printf("----------------------------------------\r\n");
    os_printf("  共发现 %d 个 WiFi 热点\r\n", count);
    os_printf("========================================\r\n\r\n");

    g_wifi_state = WIFI_STATE_CONNECTING;
}

/* ========== 延迟触发扫描 ========== */
static void ICACHE_FLASH_ATTR start_scan_cb(void *arg)
{
    struct scan_config config;
    memset(&config, 0, sizeof(config));
    config.show_hidden = 0;
    wifi_station_scan(&config, scan_done_cb);
}

/* ========== 定时打印连接状态 ========== */
static void ICACHE_FLASH_ATTR status_timer_cb(void *arg)
{
    STATION_STATUS sta_status = wifi_station_get_connect_status();
    if (sta_status != STATION_GOT_IP) {
        os_printf("[STATUS] WiFi 未连接 (状态=%d)...\r\n", sta_status);
        return;
    }

    /* 获取 IP 信息 */
    struct ip_info ip;
    if (wifi_get_ip_info(STATION_IF, &ip)) {
        os_printf("[STATUS] IP: " IPSTR "  |  GW: " IPSTR "  |  MASK: " IPSTR "\r\n",
                 IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    }

    /* 获取信号强度 */
    sint8 rssi = wifi_station_get_rssi();
    os_printf("[STATUS] 信号强度: %d dBm\r\n", rssi);

    /* 获取连接 AP 信息 */
    os_printf("[STATUS] 连接状态: %d (0=IDLE, 1=CONNECTING, 5=GOT_IP)\r\n\r\n", sta_status);
}

/* ========== WiFi 事件回调 ========== */
static void ICACHE_FLASH_ATTR wifi_event_handler(System_Event_t *event)
{
    if (event == NULL) return;

    switch (event->event_id) {

        case EVENT_STAMODE_SCAN_DONE:
            os_printf("[EVENT] 扫描完成\r\n");
            scan_done_cb(event->event_info.scan_done.bss, event->event_info.scan_done.status);
            break;

        case EVENT_STAMODE_CONNECTED: {
            Event_StaMode_Connected_t *ev = &event->event_info.connected;
            os_printf("[EVENT] 已关联到 AP, SSID: %s, CH: %d\r\n",
                     ev->ssid, ev->channel);
            break;
        }

        case EVENT_STAMODE_GOT_IP: {
            Event_StaMode_Got_IP_t *ev = &event->event_info.got_ip;
            os_printf("[EVENT] 已获取 IP 地址!\r\n");
            os_printf("[EVENT] IP: " IPSTR "\r\n", IP2STR(&ev->ip));
            os_printf("[EVENT] GW: " IPSTR "\r\n", IP2STR(&ev->gw));
            os_printf("[EVENT] MASK: " IPSTR "\r\n", IP2STR(&ev->mask));
            g_wifi_state = WIFI_STATE_CONNECTED;

            /* 启动状态定时器，每 10 秒打印连接状态 */
            os_timer_disarm(&g_status_timer);
            os_timer_setfn(&g_status_timer, (os_timer_func_t *)status_timer_cb, NULL);
            os_timer_arm(&g_status_timer, 10000, 1);  /* 每 10 秒重复 */
            break;
        }

        case EVENT_STAMODE_DISCONNECTED: {
            Event_StaMode_Disconnected_t *ev = &event->event_info.disconnected;
            os_printf("[EVENT] WiFi 断开, 原因: %d\r\n", ev->reason);
            g_wifi_state = WIFI_STATE_CONNECTING;

            /* SDK 默认会自动重连，这里只更新状态 */
            os_timer_disarm(&g_status_timer);
            break;
        }

        default:
            break;
    }
}

/**
 * 用户初始化入口 (ESP8266 RTOS SDK 规范)
 */
void user_init(void)
{
    os_printf("\r\n\r\n");
    os_printf("========================================\r\n");
    os_printf("  ESP8266 WiFi 实验\r\n");
    os_printf("  硬件: ESP-01S (1MB Flash)\r\n");
    os_printf("  SDK:  %s\r\n", system_get_sdk_version());
    os_printf("  目标: %s\r\n", WIFI_SSID);
    os_printf("========================================\r\n\r\n");

    /* 注册 WiFi 事件回调 */
    wifi_set_event_handler_cb(wifi_event_handler);

    /* 初始化 WiFi (默认为 STATION+AP 模式，停掉 AP) */
    WIFI_MODE mode = wifi_get_opmode_default();
    os_printf("[INIT] 当前 WiFi 模式: %d\r\n", mode);

    /* 设置为纯 Station 模式 */
    wifi_set_opmode(STATION_MODE);

    /* 停止 SoftAP (如果开着) */
    wifi_softap_dhcps_stop();

    /* 配置 Station */
    struct station_config sta_conf;
    memset(&sta_conf, 0, sizeof(sta_conf));
    strcpy((char *)sta_conf.ssid, WIFI_SSID);
    strcpy((char *)sta_conf.password, WIFI_PASSWORD);

    wifi_station_set_config(&sta_conf);

    /* 开启 DHCP 客户端 */
    wifi_station_dhcpc_start();

    /* 设置自动重连 (默认开启，这里显式设置) */
    wifi_station_set_reconnect_policy(true);
    wifi_station_set_auto_connect(true);

    os_printf("[INIT] WiFi Station 配置完成\r\n\r\n");

    /* ---- 第一步：延迟 2 秒后先扫描 ---- */
    /* 注意: 扫描不能在 user_init 中直接调用，需要通过定时器延迟 */
    os_printf("[SCAN] 2 秒后开始扫描 WiFi 热点...\r\n");

    g_wifi_state = WIFI_STATE_SCANNING;

    /* 延迟 2 秒再扫描（不能在 user_init 中直接调用扫描） */
    os_timer_disarm(&g_scan_timer);
    os_timer_setfn(&g_scan_timer, (os_timer_func_t *)start_scan_cb, NULL);
    os_timer_arm(&g_scan_timer, 2000, 0);  /* 2 秒后执行扫描 */

    /* 扫描完成后会自动连接 (在 scan_done_cb 中触发) */

    os_printf("[INIT] 初始化完成，等待扫描和连接...\r\n");
}
