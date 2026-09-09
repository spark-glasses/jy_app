/**
 * @file system_runtime_state.c
 * @brief 系统运行时状态同步实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system_runtime_state.h"

#include "app_lcd.h"
#include "app_def.h"
#include "common/app_framework/app_router.h"
#include "system/popups/notify/notify.h"
#include "system/system_notification.h"
#include "system/system.h"
#include "system/system_runtime_input.h"
#include "system/system_runtime_ui.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#define SYSTEM_ALS_RAW_MAX_VALUE 2142208U ///< ALS 原始光感最大值，对应最亮环境。

/**
 * @brief ALS 原始值到屏幕亮度的分档映射项。
 */
typedef struct {
    uint32_t raw_max;     ///< 当前档位包含的最大 ALS 原始值。
    uint8_t brightness;   ///< 当前档位对应的 LCD 亮度值，范围 0-255。
} system_als_brightness_level_t;

static const system_als_brightness_level_t s_als_brightness_levels[] = {
    {178517U, 32U},
    {357034U, 64U},
    {714069U, 112U},
    {1071104U, 160U},
    {1606656U, 208U},
    {SYSTEM_ALS_RAW_MAX_VALUE, 255U},
};

static uint8_t s_battery_percent = 0;   ///< 当前缓存电量百分比
static uint8_t s_charge_state_sys = 0;  ///< 当前缓存充电状态
static uint16_t s_voltage_mv_sys = 0;   ///< 当前缓存电池电压
/**
 * @brief The three independent inputs every visible state derives from.
 *
 * `link` and `display` are firmware-owned. `listen` is phone-owned and is
 * cleared when the link drops. Setters mutate a copy and hand the previous and
 * next snapshots to `system_runtime_state_apply()`, which performs every side
 * effect exactly once per transition.
 */
typedef struct {
    bool link_connected;          ///< 手机主机链路是否已连接
    bool display_on;              ///< LCD 是否亮屏
    system_listen_state_t listen; ///< 手机声明的采集会话状态
} system_runtime_snapshot_t;

/* The LCD driver starts lit, so the reducer starts lit as well. */
static system_runtime_snapshot_t s_state = {
    .link_connected = false,
    .display_on = true,
    .listen = SYSTEM_LISTEN_STATE_IDLE,
};
static bool s_device_state_btconn_synced = false; ///< 启动后是否已用设备快照初始化蓝牙连接态
static bool s_call_seen_ringing = false;       ///< 当前通话流程是否出现过振铃态
static bool s_call_seen_connected = false;     ///< 当前通话流程是否出现过接通态
static char s_call_last_number[64] = {0};      ///< 当前通话流程缓存的来电号码
static bool s_auto_brightness_valid = false;   ///< 是否已有 ALS 自动亮度目标值。
static uint8_t s_auto_brightness = 0;          ///< 最近一次 ALS 分档得到的自动亮度目标值。

/**
 * @brief 蓝牙通话建立阶段状态定义。
 */
typedef enum {
    SYSTEM_BT_CALL_EVENT_RINGING = JYT_CALL_EVENT_CALLING,        ///< 来电振铃
    SYSTEM_BT_CALL_EVENT_CONNECTED = JYT_CALL_EVENT_CONNECTED,    ///< 电话接通
    SYSTEM_BT_CALL_EVENT_DISCONNECTED = JYT_CALL_EVENT_DISCONNECTED, ///< 电话断开
} system_bt_call_setup_state_t;

/**
 * @brief 重置当前通话流程缓存。
 * @return 无返回值。
 */
static void system_runtime_state_reset_call_flow(void) {
    floatair_info("reset call flow: ringing=%d connected=%d last_number=%s",
                  (int)s_call_seen_ringing,
                  (int)s_call_seen_connected,
                  s_call_last_number[0] ? s_call_last_number : "N/A");
    s_call_seen_ringing = false;
    s_call_seen_connected = false;
    s_call_last_number[0] = '\0';
}

/**
 * @brief 缓存当前通话号码。
 * @param[in] caller_number 当前号码字符串。
 * @return 无返回值。
 */
static void system_runtime_state_cache_call_number(const char* caller_number) {
    if (caller_number == NULL || caller_number[0] == '\0') {
        return;
    }

    strncpy(s_call_last_number, caller_number, sizeof(s_call_last_number) - 1);
    s_call_last_number[sizeof(s_call_last_number) - 1] = '\0';
    floatair_info("cache call number: %s", s_call_last_number);
}

/**
 * @brief 获取当前缓存电量。
 * @return 返回当前缓存电量百分比。
 */
static uint8_t system_runtime_state_get_battery(void) {
    return s_battery_percent;
}

/**
 * @brief 设置当前缓存电量并同步到底部状态栏。
 * @param[in] battery 当前电量百分比。
 * @return 无返回值。
 */
static void system_runtime_state_set_battery(uint8_t battery) {
    s_battery_percent = battery;
    system_ui_update_battery(battery);
}

/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前缓存充电状态值。
 */
static uint8_t system_runtime_state_get_charge_state(void) {
    return s_charge_state_sys;
}

/**
 * @brief 设置当前缓存充电状态并同步到底部状态栏。
 * @param[in] charge_state 当前充电状态值。
 * @return 无返回值。
 */
static void system_runtime_state_set_charge_state(uint8_t charge_state) {
    s_charge_state_sys = charge_state;
    system_ui_update_charge_state(charge_state);
}

/**
 * @brief 获取当前缓存电池电压。
 * @return 返回当前缓存电池电压值。
 */
static uint16_t system_runtime_state_get_voltage(void) {
    return s_voltage_mv_sys;
}

/**
 * @brief 设置当前缓存电池电压。
 * @param[in] voltage_mv 当前电池电压值。
 * @return 无返回值。
 */
static void system_runtime_state_set_voltage(uint16_t voltage_mv) {
    s_voltage_mv_sys = voltage_mv;
}

/**
 * @brief 将 ALS 原始光感值映射为 LCD 自动亮度档位。
 * @param[in] raw_value ALS 原始光感值。
 * @return 返回 LCD 亮度值，范围 0-255。
 */
static uint8_t system_runtime_state_als_raw_to_brightness(uint32_t raw_value) {
    uint32_t clamped = raw_value;

    if (clamped > SYSTEM_ALS_RAW_MAX_VALUE) {
        clamped = SYSTEM_ALS_RAW_MAX_VALUE;
    }

    for (size_t i = 0; i < sizeof(s_als_brightness_levels) / sizeof(s_als_brightness_levels[0]); ++i) {
        if (clamped <= s_als_brightness_levels[i].raw_max) {
            return s_als_brightness_levels[i].brightness;
        }
    }

    return s_als_brightness_levels[
        sizeof(s_als_brightness_levels) / sizeof(s_als_brightness_levels[0]) - 1U].brightness;
}

/**
 * @brief 应用一次电池状态快照到运行时缓存，并按需同步 UI/上报。
 * @param[in] bat_status 电池状态快照。
 * @param[in] report_changed `true` 表示状态变化时同步上报，`false` 表示仅刷新本地缓存。
 * @return 无返回值。
 */
static void system_runtime_state_apply_bat_status(union bat_state_t bat_status, bool report_changed) {
    uint8_t soc = bat_status.bat_chg_combo.soc;
    uint16_t voltage_mv = bat_status.bat_chg_combo.voltage_mv;
    uint8_t charge_state = bat_status.bat_chg_combo.charger_mode;

    floatair_info("soc %d, voltage_mv %d, charge_state %d", soc, voltage_mv, charge_state);

    if (soc != system_runtime_state_get_battery()) {
        floatair_dbg("battery changed %d to %d", (int)system_runtime_state_get_battery(), (int)soc);
        system_runtime_state_set_battery(soc);
        if (report_changed) {
            system_report_battery(soc);
        }
    }
    if (voltage_mv != system_runtime_state_get_voltage()) {
        floatair_dbg("voltage changed %d to %d", (int)system_runtime_state_get_voltage(), (int)voltage_mv);
        system_runtime_state_set_voltage(voltage_mv);
    }
    if (charge_state != system_runtime_state_get_charge_state()) {
        floatair_dbg("charge state changed %d to %d",
                     (int)system_runtime_state_get_charge_state(),
                     (int)charge_state);
        system_runtime_state_set_charge_state(charge_state);
        if (report_changed) {
            system_report_charge_state(charge_state);
        }
    }
}

/**
 * @brief Apply every side effect of one snapshot transition.
 * @param[in] prev 变更前快照。
 * @param[in] next 变更后快照。
 * @param[in] source 触发来源，仅用于日志。
 * @return 无返回值。
 */
static void system_runtime_state_apply(const system_runtime_snapshot_t* prev,
                                       const system_runtime_snapshot_t* next,
                                       const char* source) {
    bool link_changed = prev->link_connected != next->link_connected;
    bool display_changed = prev->display_on != next->display_on;
    bool listen_changed = prev->listen != next->listen;

    floatair_info("runtime state: link=%d->%d display=%d->%d listen=%d->%d source=%s",
                  (int)prev->link_connected, (int)next->link_connected,
                  (int)prev->display_on, (int)next->display_on,
                  (int)prev->listen, (int)next->listen,
                  source != NULL ? source : "unknown");

    if (link_changed && !next->link_connected) {
        // A transport loss ends phone call state, but keeps local content and setup.
        system_runtime_state_reset_call_flow();
    }

    if (display_changed) {
        if (next->display_on) {
            system_request_os_sleep(false);
            floatair_lcd_set_state(LCD_ON);
            /* Resync the clock after the OS wake, before the first refresh. */
            system_update_time();
            system_ui_flush_pending_after_screen_on();
            app_sleep_timer_reset();
        } else {
            floatair_lcd_set_state(LCD_OFF);
        }
    }

    /* After the blocking display wake and refresh, so the roll-in is not cut. */
    if (link_changed || display_changed || listen_changed) {
        system_ui_sync_avatar_state(source);
    }
    if (link_changed) {
        system_ui_sync_shell_state();
    }

    if (display_changed) {
        uint8_t sys_state = next->display_on ? 1 : 0;

        (void)system_runtime_input_notify_sys_state(sys_state);
        if (next->link_connected) {
            (void)system_report_sys_state(sys_state);
        }
        if (!next->display_on) {
            system_request_os_sleep(true);
        }
    }
}

/**
 * @brief 刷新蓝牙连接状态并同步相关 UI。
 * @param[in] connected `true` 表示已连接，`false` 表示未连接。
 * @return 无返回值。
 */
static void system_runtime_state_refresh_btconn_state(bool connected) {
    system_runtime_snapshot_t prev = s_state;

    floatair_info("refresh btconn state: prev=%d, next=%d, app=%s",
                  (int)prev.link_connected,
                  (int)connected,
                  app_router_get_app());
    s_state.link_connected = connected;
    if (!connected) {
        /* The phone's capture dies with the link; do not wait for its stop. */
        s_state.listen = SYSTEM_LISTEN_STATE_IDLE;
    }
    system_runtime_state_apply(&prev, &s_state, "bt_connection");
    if (prev.link_connected == connected) {
        /* The boot snapshot can repeat the default; the overlay still needs its first sync. */
        system_ui_sync_shell_state();
    }
}

bool system_runtime_state_get_display_on(void) {
    return s_state.display_on;
}

void system_runtime_state_set_display_on(bool on, const char* source) {
    system_runtime_snapshot_t prev = s_state;

    if (prev.display_on == on) {
        if (on) {
            /* A repeated wake request keeps the screen awake. */
            app_sleep_timer_reset();
        }
        floatair_info("display already %s source=%s", on ? "on" : "off",
                      source != NULL ? source : "unknown");
        return;
    }
    s_state.display_on = on;
    system_runtime_state_apply(&prev, &s_state, source);
}

system_listen_state_t system_runtime_state_get_listen_state(void) {
    return s_state.listen;
}

void system_runtime_state_set_listen_state(system_listen_state_t state, const char* source) {
    system_runtime_snapshot_t prev = s_state;

    if (prev.listen == state) {
        return;
    }
    s_state.listen = state;
    system_runtime_state_apply(&prev, &s_state, source);
}

/**
 * @brief 处理设备状态消息并同步时间，启动首次额外同步蓝牙连接态。
 * @param[in] msg 设备状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_device_state(JYT_ELF_MQ_MSG* msg) {
    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    if (msg->payload_len != sizeof(jyt_device_state_t)) {
        floatair_err("msg payload len is not match[%d][%d]", msg->payload_len, sizeof(jyt_device_state_t));
        return false;
    }

    floatair_dbg("len %d bytes", msg->payload_len);
    jyt_device_state_t device_state;
    memcpy(&device_state, msg->payload, sizeof(device_state));
    floatair_info("device state update: time_now=%" PRIu64 " host_connected=%u speaker_connected=%u btconn_synced=%d",
                  (uint64_t)device_state.time_now,
                  (unsigned)device_state.host_connected,
                  (unsigned)device_state.speaker_connected,
                  (int)s_device_state_btconn_synced);
    if (!s_device_state_btconn_synced) {
        system_runtime_state_refresh_btconn_state(device_state.host_connected != 0);
        s_device_state_btconn_synced = true;
    }
    if (!system_ui_update_time_from_epoch(device_state.time_now)) {
        return false;
    }

    return true;
}

/**
 * @brief 获取 LCD 亮屏恢复时应使用的亮度。
 * @return 自动亮度开启且已有 ALS 档位时返回自动亮度，否则返回保存的手动亮度。
 */
uint8_t system_runtime_state_get_lcd_resume_brightness(void) {
    if (system_config_get_bl_auto() && s_auto_brightness_valid) {
        return s_auto_brightness;
    }

    return system_config_get_brightness();
}

/**
 * @brief 将最近一次缓存的 ALS 自动亮度立即应用到当前亮屏。
 * @return `true` 表示无需更新或更新成功，`false` 表示亮度上报失败。
 */
bool system_runtime_state_apply_auto_brightness(void) {
    if (!system_config_get_bl_auto() || !s_auto_brightness_valid) {
        return true;
    }
    if (floatair_lcd_is_off()) {
        floatair_info(
            "als brightness deferred while lcd off: %u", (unsigned)s_auto_brightness);
        return true;
    }
    if ((uint8_t)floatair_lcd_get_brightness() == s_auto_brightness) {
        return true;
    }

    floatair_lcd_set_brightness(s_auto_brightness);
    return system_report_brightness(s_auto_brightness);
}

/**
 * @brief 处理 ALS 原始光感数据并在自动亮度开启时刷新屏幕亮度。
 * @param[in] msg ALS 原始数据消息，payload 为 uint32_t 原始光感值。
 * @return `true` 表示处理成功，`false` 表示消息格式错误。
 */
bool system_update_als_raw_data(JYT_ELF_MQ_MSG* msg) {
    uint32_t raw_value = 0;
    uint8_t brightness = 0;

    if (msg == NULL) {
        floatair_err("als raw msg is NULL");
        return false;
    }
    if (msg->payload_len < sizeof(raw_value)) {
        floatair_err("invalid als raw payload_len: %d", msg->payload_len);
        return false;
    }

    memcpy(&raw_value, msg->payload, sizeof(raw_value));
    brightness = system_runtime_state_als_raw_to_brightness(raw_value);
    floatair_info("als raw update: raw=%" PRIu32 " brightness=%u auto=%d",
                  raw_value,
                  (unsigned)brightness,
                  (int)system_config_get_bl_auto());

    s_auto_brightness = brightness;
    s_auto_brightness_valid = true;
    if (!system_config_get_bl_auto()) {
        return true;
    }
    return system_runtime_state_apply_auto_brightness();
}

/**
 * @brief Wake the screen on a keyword hit when Bluetooth is connected.
 * @param[in] msg KWS 事件消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_kws_state(JYT_ELF_MQ_MSG* msg) {
    const char* current_app = app_router_get_app();

    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }

    uint32_t kws_hit = msg->Header.simple_data;
    floatair_dbg("kws_hit: %" PRIu32, kws_hit);
    if (!system_config_get_keyword_spotting_enabled()) {
        floatair_info("keyword spotting disabled, ignore kws hit=%" PRIu32, kws_hit);
        return true;
    }
    uint32_t configured_kws_hit = system_config_get_kws_hit_value();
    if (kws_hit != configured_kws_hit) {
        floatair_info("ignore unmatched kws hit=%" PRIu32 " configured=%" PRIu32,
                      kws_hit,
                      configured_kws_hit);
        return true;
    }
    /* Spark home does not run the vendor home tutorial. */
    floatair_info("kws state update: hit=%" PRIu32 " display_on=%d current_app=%s",
                  kws_hit,
                  (int)s_state.display_on,
                  current_app);
    if (!system_get_btconn_state()) {
        floatair_info("ignore kws wake while bt disconnect overlay active");
        return true;
    }

    system_runtime_state_set_display_on(true, "kws");
    return true;
}

/**
 * @brief 处理来电状态消息并控制来电通知显隐。
 * @param[in] msg 来电状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_handle_call_setup_event(JYT_ELF_MQ_MSG* msg) {
    char caller_number[64] = {0};
    uint8_t raw_state = 0;
    size_t caller_len = 0;

    if (msg == NULL || msg->payload_len == 0) {
        floatair_err("call setup msg invalid");
        return false;
    }

    raw_state = msg->payload[0];
    if (msg->payload_len > 1) {
        caller_len = msg->payload_len - 1U;
        if (caller_len >= sizeof(caller_number)) {
            caller_len = sizeof(caller_number) - 1U;
        }
        memcpy(caller_number, msg->payload + 1, caller_len);
    }

    floatair_info("call setup state=%u, payload_len=%u, caller=%s",
                  (unsigned)raw_state,
                  (unsigned)msg->payload_len,
                  caller_number[0] ? caller_number : "N/A");

    system_runtime_state_cache_call_number(caller_number);

    switch ((system_bt_call_setup_state_t)raw_state) {
        case SYSTEM_BT_CALL_EVENT_CONNECTED:
            s_call_seen_connected = true;
            floatair_info("call flow connected: dismiss notification, caller=%s",
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            system_notification_dismiss_call();
            return true;
        case SYSTEM_BT_CALL_EVENT_DISCONNECTED:
            floatair_info("call flow disconnected: ringing=%d connected=%d bt_connected=%d last_number=%s",
                          (int)s_call_seen_ringing,
                          (int)s_call_seen_connected,
                          (int)system_get_btconn_state(),
                          s_call_last_number[0] ? s_call_last_number : "N/A");
            system_notification_dismiss_call();
            if (s_call_seen_ringing && !s_call_seen_connected) {
                if (system_get_btconn_state()) {
                    system_notification_show_missed_call(s_call_last_number[0] ? s_call_last_number : NULL);
                } else {
                    floatair_info("suppress missed call notification while bt disconnect overlay active");
                }
            }
            system_runtime_state_reset_call_flow();
            return true;
        case SYSTEM_BT_CALL_EVENT_RINGING:
            s_call_seen_ringing = true;
            s_call_seen_connected = false;
            floatair_info("call flow ringing: bt_connected=%d caller=%s",
                          (int)system_get_btconn_state(),
                          caller_number[0] ? caller_number : "N/A");
            if (!system_get_btconn_state()) {
                floatair_info("suppress incoming call notification while bt disconnect overlay active");
                return true;
            }
            floatair_info("show incoming call notification without generated title");
            return system_notification_show_call(NULL,
                                                 caller_number[0] ? caller_number : NULL);
        default:
            floatair_warn("unknown call setup state=%u", (unsigned)raw_state);
            system_notification_dismiss_call();
            system_runtime_state_reset_call_flow();
            return false;
    }
}

/**
 * @brief 处理电池状态消息并同步缓存、状态栏与上报。
 * @param[in] msg 电池状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_bat_status(JYT_ELF_MQ_MSG* msg) {
    if (msg == NULL) {
        floatair_err("msg is NULL");
        return false;
    }
    if (msg->payload_len == 0 || msg->payload_len < sizeof(union bat_state_t)) {
        floatair_err("msg payload is NULL or empty[%d][%d]", msg->payload_len, sizeof(union bat_state_t));
        return false;
    }

    union bat_state_t bat_status;
    memcpy(&bat_status, msg->payload, sizeof(bat_status));
    system_runtime_state_apply_bat_status(bat_status, true);
    return true;
}

/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前缓存充电状态值。
 */
uint8_t system_get_charge_state(void) {
    return system_runtime_state_get_charge_state();
}

/**
 * @brief 获取当前缓存电量。
 * @return 返回当前缓存电量百分比。
 */
uint8_t system_get_battery(void) {
    return system_runtime_state_get_battery();
}

/**
 * @brief 获取当前缓存蓝牙连接状态。
 * @return `true` 表示蓝牙已连接，`false` 表示蓝牙未连接。
 */
bool system_get_btconn_state(void) {
    return s_state.link_connected;
}

/**
 * @brief 设置显式主机连接事件状态并刷新综合蓝牙连接态。
 * @param[in] connected `true` 表示显式事件为已连接，`false` 表示显式事件为未连接。
 * @return 无返回值。
 */
void system_set_btconn_state(bool connected) {
    floatair_info("set bt connection state request: connected=%d", (int)connected);
    system_runtime_state_refresh_btconn_state(connected);
}
