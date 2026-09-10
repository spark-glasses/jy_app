/**
 * @file system_runtime_state.h
 * @brief 系统运行时状态同步接口声明
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#pragma once

#include "system/system_runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief KWS 命中事件的软件拦截原因。
 */
typedef enum {
    SYSTEM_KWS_INTERCEPT_REASON_APP_POLICY = 1u << 0,    ///< 当前 App 策略禁止响应 KWS。
    SYSTEM_KWS_INTERCEPT_REASON_SPEECH_ACTIVE = 1u << 1, ///< Speech 正在运行。
    SYSTEM_KWS_INTERCEPT_REASON_PHONE_ACTIVE = 1u << 2,  ///< Phone 通话流程正在进行。
    SYSTEM_KWS_INTERCEPT_REASON_PROMPTER_RUNNING = 1u << 3, ///< 题词正在运行。
} system_kws_intercept_reason_t;

/**
 * @brief 设置或清除一项 KWS 软件拦截原因。
 * @param[in] reason 拦截原因。
 * @param[in] blocked `true` 表示设置，`false` 表示清除。
 * @return 无返回值。
 */
void system_runtime_state_set_kws_intercept(system_kws_intercept_reason_t reason,
                                            bool blocked);

/**
 * @brief 缓存 ANCS 提供的第三方来电联系人和来源，供随后的 HFP 状态使用。
 * @param[in] caller_name 第三方来电联系人名称。
 * @param[in] source_id iOS AppIdentifier。
 * @return 无返回值。
 */
void system_runtime_state_set_ancs_call_info(const char* caller_name,
                                             const char* source_id);

/**
 * @brief 处理设备状态消息并同步时间，启动首次额外同步蓝牙连接态。
 * @param[in] msg 设备状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_device_state(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 处理 ALS 原始光感数据并在自动亮度开启时刷新屏幕亮度。
 * @param[in] msg ALS 原始数据消息，payload 为 uint32_t 原始光感值。
 * @return `true` 表示处理成功，`false` 表示消息格式错误。
 */
bool system_update_als_raw_data(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 将最近一次缓存的 ALS 自动亮度立即应用到当前亮屏。
 * @return `true` 表示无需更新或更新成功，`false` 表示亮度上报失败。
 */
bool system_runtime_state_apply_auto_brightness(void);
/**
 * @brief 获取 LCD 亮屏恢复时应使用的亮度。
 * @return 自动亮度开启且已有 ALS 档位时返回自动亮度，否则返回保存的手动亮度。
 */
uint8_t system_runtime_state_get_lcd_resume_brightness(void);
/**
 * @brief 处理 KWS 命中事件，通过软件策略过滤后唤醒屏幕和上报命中。
 * @param[in] msg KWS 事件消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_kws_state(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 处理来电状态消息并控制来电通知显隐。
 * @param[in] msg 来电状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_handle_call_setup_event(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 处理电池状态消息并同步缓存、状态栏与上报。
 * @param[in] msg 电池状态消息。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool system_update_bat_status(JYT_ELF_MQ_MSG* msg);
/**
 * @brief 获取当前缓存充电状态。
 * @return 返回当前充电状态值。
 */
uint8_t system_get_charge_state(void);
/**
 * @brief 获取当前缓存电量。
 * @return 返回当前电量百分比。
 */
uint8_t system_get_battery(void);
/**
 * @brief 获取当前蓝牙连接状态。
 * @return `true` 表示蓝牙已连接，`false` 表示蓝牙未连接。
 */
bool system_get_btconn_state(void);
/**
 * @brief 获取蓝牙连接状态变化事件 ID。
 * @return 返回 LVGL 自定义事件 ID，事件参数为指向 `uint8_t` 连接状态的指针。
 */
uint32_t system_runtime_state_get_btconn_event(void);
/**
 * @brief 设置显式主机连接事件状态并刷新蓝牙连接态。
 * @param[in] connected `true` 表示显式事件为已连接，`false` 表示显式事件为未连接。
 * @return 无返回值。
 */
void system_set_btconn_state(bool connected);
/**
 * @brief 亮屏后补做灭屏期间延迟的蓝牙状态 UI 同步。
 * @return 无返回值。
 */
void system_runtime_state_flush_pending_after_screen_on(void);

#ifdef __cplusplus
}
#endif
