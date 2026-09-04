#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define SYSTEM_LVGL_TICK_PERIOD (200) // 200ms
#define SYSTEM_LVGL_SECOND_PERIOD (1000) // 1 second 1000ms
#define SYSTEM_LVGL_MINUTE_PERIOD (1000 * 60) // 1 minute

#define SYSTEM_TIMER_ID_SLEEP 0 // sleep timer
#define SYSTEM_TIMER_ID_LVGL_PERIOD 1 // lvgl period timer
#define SYSTEM_TIMER_ID_LVGL_REALIGN 2 // 对齐首次 LVGL 分钟刷新的一次性定时器。
#define SYSTEM_TIMER_ID_OS_SLEEP_DELAY 3 // 息屏后延迟允许 OS 休眠的定时器。
#define SYSTEM_OS_SLEEP_DELAY_MS 30000U // 息屏后等待允许 OS 休眠的时长。

#define SYSTEM_TIMER_AUTO_DESTROY1 11 // auto destroy timer
#define SYSTEM_TIMER_AUTO_DESTROY2 12 // auto destroy timer 2
#define SYSTEM_TIMER_AUTO_DESTROY3 13 // auto destroy timer 3
#define SYSTEM_TIMER_AUTO_DESTROY4 14 // auto destroy timer 4
#define SYSTEM_TIMER_AUTO_DESTROY5 15 // auto destroy timer 5

typedef void (*system_timer_autodestroy_cb_t)(void* user_data);

void system_timer_init(void);
void system_timer_deinit(void);

bool system_timer_autodestroy_start(uint32_t timeout_ms,
                                   system_timer_autodestroy_cb_t cb,
                                   void* user_data,
                                   uint32_t* out_timer_id);
bool system_timer_autodestroy_cancel(uint32_t timer_id);
bool system_timer_handle_trigger(uint32_t timer_id);
/**
 * @brief 亮屏后执行灭屏期间到期而延迟的自动销毁定时器回调。
 * @return 无返回值。
 */
void system_timer_flush_pending_after_screen_on(void);

bool system_timer_sleep_start(uint32_t timeout_ms);
/**
 * @brief 重启现有息屏定时器。
 * @return `true` 表示定时器存在且已重启，`false` 表示当前没有可重启的定时器。
 */
bool system_timer_sleep_reset(void);
void system_timer_sleep_deinit(void);

/**
 * @brief 启动息屏后的延迟休眠定时器。
 * @return `true` 表示启动成功，`false` 表示启动失败。
 */
bool system_timer_os_sleep_delay_start(void);
/**
 * @brief 取消延迟休眠并禁止 OS 休眠。
 * @return 无返回值。
 */
void system_timer_os_sleep_delay_cancel(void);

bool system_timer_lvgl_period_start(void);
bool system_timer_lvgl_period_realign_to_minute(time_t time_now);
void system_timer_lvgl_period_stop(void);
