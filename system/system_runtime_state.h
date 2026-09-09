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
 * @brief Wake the screen on a matching keyword hit when Bluetooth is connected.
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
 * @brief Phone-declared capture session state.
 *
 * The phone owns audio capture. It reports `LISTENING` once glasses audio is
 * flowing and `IDLE` when capture stops. A lost link resets it to `IDLE`.
 */
typedef enum {
    SYSTEM_LISTEN_STATE_IDLE = 0,
    SYSTEM_LISTEN_STATE_LISTENING = 1,
} system_listen_state_t;

/**
 * @brief 获取当前显示状态。
 * @return `true` 表示亮屏，`false` 表示灭屏。
 */
bool system_runtime_state_get_display_on(void);
/**
 * @brief Set the display state. Every screen transition goes through here.
 *
 * Turning an already lit screen on only restarts the sleep timer. A change
 * drives the LCD driver, OS sleep permission, pending UI flushes, the avatar,
 * the page screen event, and the phone report from one place.
 * @param[in] on `true` 表示亮屏，`false` 表示灭屏。
 * @param[in] source 触发来源，仅用于日志。
 * @return 无返回值。
 */
void system_runtime_state_set_display_on(bool on, const char* source);
/**
 * @brief 获取手机声明的采集会话状态。
 * @return 当前采集会话状态。
 */
system_listen_state_t system_runtime_state_get_listen_state(void);
/**
 * @brief Record the phone's capture session state and resync the avatar.
 * @param[in] state 手机声明的采集会话状态。
 * @param[in] source 触发来源，仅用于日志。
 * @return 无返回值。
 */
void system_runtime_state_set_listen_state(system_listen_state_t state, const char* source);
/**
 * @brief 获取当前蓝牙连接状态。
 * @return `true` 表示蓝牙已连接，`false` 表示蓝牙未连接。
 */
bool system_get_btconn_state(void);
/**
 * @brief 设置显式主机连接事件状态并刷新蓝牙连接态。
 * @param[in] connected `true` 表示显式事件为已连接，`false` 表示显式事件为未连接。
 * @return 无返回值。
 */
void system_set_btconn_state(bool connected);

#ifdef __cplusplus
}
#endif
