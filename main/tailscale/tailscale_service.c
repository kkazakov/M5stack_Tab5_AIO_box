/**
 * @file tailscale_service.c
 * @brief Client-only Tailscale service (MicroLink-based) for the M5Stack Tab5.
 *
 *  - creates the MicroLink instance (interactive/QR login, no auth key)
 *  - starts/stops the VPN following the Wi-Fi state (esp_hosted STA on the
 *    ESP32-C6 — MicroLink itself never touches Wi-Fi) and the user's
 *    app_settings "tailscale_en" toggle
 *  - polls the connection state, logs transitions, surfaces the login URL
 *  - exposes status getters and a hostname autodetect helper for the SSH client
 *
 * Future work: make the device name a user setting and gate the first start
 * on a valid system time (TLS certificate verification).
 */

#include "tailscale_service.h"
#include "settings/app_settings.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "tailscale"

/* Device name lives in tailscale_service.h (TAILSCALE_DEFAULT_DEVICE_NAME);
 * Phase 1 will make it a user setting (app_settings). */

#define TS_POLL_INTERVAL_MS 500
#define TS_TASK_STACK       4096
#define TS_TASK_PRIO        3

/* Tailnet CGNAT range: 100.64.0.0/10 -> first two octets 100.64..100.127 */
#define TS_CGNAT_BASE 0x64400000u /* 100.64.0.0 */
#define TS_CGNAT_MASK 0xFFC00000u /* /10 */

static microlink_t       *s_ml;
static bool               s_wifi_connected;
static microlink_state_t  s_state = ML_STATE_IDLE;
static tailscale_state_cb_t s_cb;
static void              *s_cb_user_data;

static const char *state_name(microlink_state_t st)
{
    switch (st) {
    case ML_STATE_IDLE:         return "idle";
    case ML_STATE_WIFI_WAIT:    return "wifi-wait";
    case ML_STATE_CONNECTING:   return "connecting";
    case ML_STATE_REGISTERING:  return "registering";
    case ML_STATE_CONNECTED:    return "connected";
    case ML_STATE_RECONNECTING: return "reconnecting";
    case ML_STATE_ERROR:        return "error";
    case ML_STATE_AUTH_FAILED:  return "auth-failed";
    case ML_STATE_NEEDS_LOGIN:  return "needs-login";
    default:                    return "unknown";
    }
}

static void notify_state(microlink_state_t st)
{
    if (st == s_state) {
        return;
    }
    s_state = st;
    ESP_LOGI(TAG, "state: %s", state_name(st));

    if (st == ML_STATE_NEEDS_LOGIN && s_ml) {
        char url[MICROLINK_AUTH_URL_MAX];
        if (microlink_get_auth_url(s_ml, url, sizeof(url)) == ESP_OK) {
            /* The URL is a credential until used: keep it off shared logs. */
            ESP_LOGI(TAG, "Tailscale login needed - open this URL on another device:");
            ESP_LOGI(TAG, "  %s", url);
        }
    }
    if (s_cb) {
        s_cb(st, s_cb_user_data);
    }
}

/* MicroLink's own callback fires only on CONNECTED / RECONNECTING /
 * AUTH_FAILED; the poll task below covers the rest (NEEDS_LOGIN etc.). */
static void ml_state_cb(microlink_t *ml, microlink_state_t state, void *user_data)
{
    (void)ml;
    (void)user_data;
    notify_state(state);
}

static void ts_poll_task(void *arg)
{
    (void)arg;
    while (true) {
        bool enabled = app_settings_get_tailscale_enabled();
        microlink_state_t st = s_ml ? microlink_get_state(s_ml) : ML_STATE_IDLE;
        bool started = (st != ML_STATE_IDLE);

        if (enabled && s_wifi_connected && !started) {
            ESP_LOGI(TAG, "Wi-Fi up - starting VPN");
            esp_err_t err = microlink_start(s_ml);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "microlink_start: %s", esp_err_to_name(err));
            }
        } else if ((!enabled || !s_wifi_connected) && started) {
            ESP_LOGI(TAG, "%s - stopping VPN",
                     enabled ? "Wi-Fi down" : "Tailscale disabled");
            esp_err_t err = microlink_stop(s_ml);
            if (err == ESP_ERR_TIMEOUT) {
                /* A task is still blocked; retry on the next poll. */
                ESP_LOGW(TAG, "microlink_stop timed out, will retry");
            }
        }

        notify_state(s_ml ? microlink_get_state(s_ml) : ML_STATE_IDLE);
        vTaskDelay(pdMS_TO_TICKS(TS_POLL_INTERVAL_MS));
    }
}

esp_err_t tailscale_service_init(void)
{
    microlink_config_t cfg = {
        .auth_key = NULL,        /* interactive (QR) login */
        .device_name = TAILSCALE_DEFAULT_DEVICE_NAME,
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = 0,          /* CONFIG_ML_MAX_PEERS */
        .wifi_tx_power_dbm = 0,  /* never touch esp_wifi (Wi-Fi is on the C6) */
    };

    s_ml = microlink_init(&cfg);
    if (!s_ml) {
        ESP_LOGE(TAG, "microlink_init failed");
        return ESP_FAIL;
    }
    microlink_set_state_callback(s_ml, ml_state_cb, NULL);

    if (xTaskCreate(ts_poll_task, "tailscale", TS_TASK_STACK, NULL,
                    TS_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "poll task create failed");
        microlink_destroy(s_ml);
        s_ml = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "service ready (client-only, QR login)");
    return ESP_OK;
}

void tailscale_service_on_wifi_state(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_CONNECTED:
        s_wifi_connected = true;
        break;
    case WIFI_STATE_DISCONNECTED:
    case WIFI_STATE_FAILED:
    case WIFI_STATE_IDLE:
        s_wifi_connected = false;
        break;
    case WIFI_STATE_CONNECTING:
        /* Keep the current session while reconnecting. */
        break;
    }
}

void tailscale_service_set_state_cb(tailscale_state_cb_t cb, void *user_data)
{
    s_cb = cb;
    s_cb_user_data = user_data;
}

bool tailscale_service_is_available(void)
{
    return s_ml != NULL;
}

bool tailscale_service_is_connected(void)
{
    return s_ml && microlink_is_connected(s_ml);
}

microlink_state_t tailscale_service_get_state(void)
{
    return s_ml ? microlink_get_state(s_ml) : ML_STATE_ERROR;
}

microlink_t *tailscale_service_get_handle(void)
{
    return s_ml;
}

uint32_t tailscale_service_get_vpn_ip(void)
{
    return s_ml ? microlink_get_vpn_ip(s_ml) : 0;
}

void tailscale_service_get_vpn_ip_str(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return;
    }
    microlink_ip_to_str(tailscale_service_get_vpn_ip(), buf);
}

esp_err_t tailscale_service_get_auth_url(char *buf, size_t len)
{
    if (!s_ml) {
        return ESP_ERR_INVALID_STATE;
    }
    return microlink_get_auth_url(s_ml, buf, len);
}

uint32_t tailscale_service_resolve_peer(const char *host)
{
    if (!s_ml || !host || host[0] == '\0') {
        return 0;
    }

    /* Literal IPv4: only tailnet CGNAT addresses (100.64.0.0/10) count. */
    uint32_t ip = microlink_parse_ip(host);
    if (ip != 0) {
        return ((ip & TS_CGNAT_MASK) == TS_CGNAT_BASE) ? ip : 0;
    }

    /* Short name or FQDN against the known peer list (no network calls). */
    return microlink_resolve(s_ml, host);
}
