/* ---------------------------------------------------------------------------
 * Tailscale settings page (client-only, on-screen QR login).
 *
 * Rendered lazily like the other settings pages; a 1 s LVGL timer refreshes
 * the status rows and shows the login URL/QR while ML_STATE_NEEDS_LOGIN.
 * The QR code is drawn into an RGB565 LVGL canvas from a PSRAM buffer.
 * ------------------------------------------------------------------------- */

#define TS_QR_MAX_VERSION 13
#define TS_QR_MAX_PX      ((TS_QR_MAX_VERSION * 4 + 17) * 6)
#define TS_QR_BUF_MAX_BYTES (TS_QR_MAX_PX * TS_QR_MAX_PX * 2)

static lv_obj_t *tailscale_create_row(lv_obj_t *parent, const char *name,
                                      const char *value, lv_obj_t **out_value)
{
    lv_obj_t *row = create_row(parent, name, value);
    if (out_value) {
        /* create_row() appends the value label last. */
        *out_value = lv_obj_get_child(row, -1);
    }
    return row;
}

static void render_tailscale_qr(const char *url);

static void on_tailscale_enable_toggle(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    esp_err_t err = app_settings_set_tailscale_enabled(enabled);
    ESP_LOGI(TAG, "tailscale enabled=%d (save %s)",
             enabled ? 1 : 0, esp_err_to_name(err));
}

static const char *tailscale_state_text(bool enabled, microlink_state_t st)
{
    if (!enabled) {
        return TRS("已禁用");
    }
    switch (st) {
    case ML_STATE_IDLE:         return TRS("未启动");
    case ML_STATE_WIFI_WAIT:    return TRS("等待 WiFi");
    case ML_STATE_CONNECTING:   return TRS("连接中");
    case ML_STATE_REGISTERING:  return TRS("注册中");
    case ML_STATE_CONNECTED:    return TRS("已连接");
    case ML_STATE_RECONNECTING: return TRS("重连中");
    case ML_STATE_AUTH_FAILED:  return TRS("登录失败");
    case ML_STATE_NEEDS_LOGIN:  return TRS("等待登录");
    case ML_STATE_ERROR:
    default:                    return TRS("错误");
    }
}

static void on_tailscale_status_timer(lv_timer_t *timer)
{
    (void)timer;
    bool enabled = app_settings_get_tailscale_enabled();
    microlink_state_t st = tailscale_service_get_state();

    if (s_tailscale_status_label) {
        lv_label_set_text(s_tailscale_status_label,
                          tailscale_state_text(enabled, st));
    }
    if (s_tailscale_ip_label) {
        char ip[16];
        tailscale_service_get_vpn_ip_str(ip, sizeof(ip));
        lv_label_set_text(s_tailscale_ip_label,
                          (enabled && st == ML_STATE_CONNECTED) ? ip : "—");
    }
    if (s_tailscale_name_label) {
        lv_label_set_text(s_tailscale_name_label, TAILSCALE_DEFAULT_DEVICE_NAME);
    }

    bool needs_login = enabled && st == ML_STATE_NEEDS_LOGIN;
    if (s_tailscale_login_section) {
        if (needs_login && lv_obj_has_flag(s_tailscale_login_section, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(s_tailscale_login_section, LV_OBJ_FLAG_HIDDEN);
        } else if (!needs_login &&
                   !lv_obj_has_flag(s_tailscale_login_section, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_tailscale_login_section, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (needs_login) {
        char url[MICROLINK_AUTH_URL_MAX];
        if (tailscale_service_get_auth_url(url, sizeof(url)) == ESP_OK) {
            if (s_tailscale_url_label) {
                lv_label_set_text(s_tailscale_url_label, url);
            }
            if (strcmp(url, s_tailscale_last_url) != 0) {
                snprintf(s_tailscale_last_url, sizeof(s_tailscale_last_url), "%s", url);
                render_tailscale_qr(url);
            }
        }
    }

    bool auth_failed = enabled && st == ML_STATE_AUTH_FAILED;
    if (s_tailscale_auth_failed_hint) {
        if (auth_failed &&
            lv_obj_has_flag(s_tailscale_auth_failed_hint, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(s_tailscale_auth_failed_hint, LV_OBJ_FLAG_HIDDEN);
        } else if (!auth_failed &&
                   !lv_obj_has_flag(s_tailscale_auth_failed_hint, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_tailscale_auth_failed_hint, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Render the login URL as a QR code into the canvas (RGB565, PSRAM buffer). */
static void render_tailscale_qr(const char *url)
{
    static uint8_t qr_temp[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(TS_QR_MAX_VERSION)];

    if (!s_tailscale_qr_canvas) {
        return;
    }
    if (!qrcodegen_encodeText(url, qr_temp, qr, qrcodegen_Ecc_LOW, 1,
                              TS_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true)) {
        ESP_LOGW(TAG, "Tailscale QR encode failed for URL (%u chars)",
                 (unsigned)strlen(url));
        return;
    }

    if (!s_tailscale_qr_buf) {
        s_tailscale_qr_buf = heap_caps_malloc(TS_QR_BUF_MAX_BYTES,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_tailscale_qr_buf) {
            s_tailscale_qr_buf = heap_caps_malloc(TS_QR_BUF_MAX_BYTES,
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!s_tailscale_qr_buf) {
            ESP_LOGE(TAG, "Tailscale QR buffer alloc failed (%u bytes)",
                     (unsigned)TS_QR_BUF_MAX_BYTES);
            return;
        }
    }

    int n = qrcodegen_getSize(qr);
    int scale = 6;
    int w = n * scale;
    size_t buf_bytes = (size_t)w * w * 2;
    uint8_t *buf = s_tailscale_qr_buf;

    memset(buf, 0xFF, buf_bytes); /* light modules = white */
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!qrcodegen_getModule(qr, x, y)) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                uint16_t *row = (uint16_t *)(buf + (size_t)(y * scale + dy) * (size_t)w * 2);
                for (int dx = 0; dx < scale; dx++) {
                    row[x * scale + dx] = 0x0000; /* dark modules = black */
                }
            }
        }
    }

    lv_canvas_set_buffer(s_tailscale_qr_canvas, buf, w, w, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_tailscale_qr_canvas, w, w);
}

static void render_tailscale_page(lv_obj_t *parent)
{
    /* Enable toggle */
    s_tailscale_enable_checkbox = lv_checkbox_create(parent);
    lv_obj_null_on_delete(&s_tailscale_enable_checkbox);
    lv_checkbox_set_text(s_tailscale_enable_checkbox, TRS("启用 Tailscale"));
    lv_obj_set_width(s_tailscale_enable_checkbox, LV_PCT(100));
    lv_obj_set_height(s_tailscale_enable_checkbox, 42);
    lv_obj_set_style_text_font(s_tailscale_enable_checkbox, &APP_FONT, 0);
    lv_obj_set_style_text_color(s_tailscale_enable_checkbox, THEME_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_tailscale_enable_checkbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_tailscale_enable_checkbox, THEME_PRIMARY,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_tailscale_enable_checkbox, THEME_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(s_tailscale_enable_checkbox, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (app_settings_get_tailscale_enabled()) {
        lv_obj_add_state(s_tailscale_enable_checkbox, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_tailscale_enable_checkbox, on_tailscale_enable_toggle,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Status section */
    lv_obj_t *section = add_info_section(parent, TRS("Tailscale"));
    tailscale_create_row(section, TRS("状态"), "—", &s_tailscale_status_label);
    tailscale_create_row(section, TRS("VPN 地址"), "—", &s_tailscale_ip_label);
    tailscale_create_row(section, TRS("设备名称"),
                         TAILSCALE_DEFAULT_DEVICE_NAME, &s_tailscale_name_label);

    /* Auth-failed hint (hidden unless the control plane rejects us) */
    s_tailscale_auth_failed_hint = create_wrap_label(
        parent,
        TRS("登录失败：设备密钥可能已过期或被撤销。请在 Tailscale 管理后台关闭该设备的密钥过期（Disable key expiry）后，再关闭并重新开启 Tailscale。"),
        THEME_WARNING_LIGHT);
    lv_obj_add_flag(s_tailscale_auth_failed_hint, LV_OBJ_FLAG_HIDDEN);

    /* Login section (hidden unless a QR login is pending) */
    s_tailscale_login_section = add_info_section(parent, TRS("登录 Tailscale"));
    lv_obj_add_flag(s_tailscale_login_section, LV_OBJ_FLAG_HIDDEN);
    s_tailscale_login_hint_label = create_wrap_label(
        s_tailscale_login_section,
        TRS("请在其他设备上打开下方链接并登录，登录完成后本机将自动加入你的 tailnet。"),
        THEME_TEXT_SECONDARY);
    lv_obj_null_on_delete(&s_tailscale_login_hint_label);
    s_tailscale_qr_canvas = lv_canvas_create(s_tailscale_login_section);
    lv_obj_null_on_delete(&s_tailscale_qr_canvas);
    lv_obj_set_style_bg_color(s_tailscale_qr_canvas, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_tailscale_qr_canvas, LV_OPA_COVER, 0);
    s_tailscale_url_label = create_wrap_label(s_tailscale_login_section, "",
                                              THEME_TEXT_TERTIARY);

    /* Refresh loop */
    s_tailscale_status_timer = lv_timer_create(on_tailscale_status_timer, 1000, NULL);
    on_tailscale_status_timer(s_tailscale_status_timer);
}
