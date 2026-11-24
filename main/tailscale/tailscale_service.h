/**
 * @file tailscale_service.h
 * @brief Client-only Tailscale integration for the M5Stack Tab5.
 *
 * Built on the vendored MicroLink component (components/microlink), a
 * Tailscale-compatible VPN client for ESP-IDF. Scope is intentionally
 * client-only:
 *
 *   - outbound TCP/UDP to tailnet peers (the SSH terminal use case)
 *   - interactive login (auth URL / QR code on screen), no auth key
 *   - hostname/IP autodetect helper for the SSH client
 *
 * Inbound services (web file manager over the tailnet), subnet router and
 * exit node are out of scope for now.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "microlink.h"
#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default tailnet device name (Phase 1 TODO: make it a user setting). */
#define TAILSCALE_DEFAULT_DEVICE_NAME "tab5"

/** Tailscale state change callback. NOTE: runs in MicroLink's task or the
 *  service poll task, never the LVGL task — do UI work by polling the
 *  getters from the LVGL task instead. */
typedef void (*tailscale_state_cb_t)(microlink_state_t state, void *user_data);

/**
 * @brief Create the MicroLink instance and start the state poll task.
 * Must be called after NVS init (microlink loads keys from NVS) and after
 * wifi_manager_init() in the boot background task.
 */
esp_err_t tailscale_service_init(void);

/** Feed Wi-Fi state changes into the service (start on CONNECTED, stop on
 *  DISCONNECTED/FAILED). Call from the wifi_manager callback. */
void tailscale_service_on_wifi_state(wifi_state_t state);

void tailscale_service_set_state_cb(tailscale_state_cb_t cb, void *user_data);

bool tailscale_service_is_available(void);
bool tailscale_service_is_connected(void);
microlink_state_t tailscale_service_get_state(void);

/** Raw handle for transport integration (e.g. SSH over the tailnet).
 *  NULL if the service failed to init. Do not destroy or stop it. */
microlink_t *tailscale_service_get_handle(void);

/** Own VPN address (host byte order), 0 if not assigned. */
uint32_t tailscale_service_get_vpn_ip(void);

/** Own VPN address as "a.b.c.d" (writes "0.0.0.0" when unassigned). */
void tailscale_service_get_vpn_ip_str(char *buf, size_t len);

/** Login URL when ML_STATE_NEEDS_LOGIN is active; ESP_ERR_NOT_FOUND otherwise. */
esp_err_t tailscale_service_get_auth_url(char *buf, size_t len);

/**
 * @brief Autodetect whether `host` is a tailnet destination, and resolve it.
 *
 * Rules:
 *   - literal IPv4 in 100.64.0.0/10 (the Tailscale CGNAT range) -> that IP
 *   - short name or FQDN matching a known peer (microlink_resolve) -> VPN IP
 *   - anything else -> 0 (caller should use the plain network stack)
 */
uint32_t tailscale_service_resolve_peer(const char *host);

#ifdef __cplusplus
}
#endif
