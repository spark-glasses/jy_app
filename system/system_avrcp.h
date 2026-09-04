/**
 * @file system_avrcp.h
 * @brief AVRCP 跨核事件解析、媒体状态缓存和页面监听接口。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "elf_common.h"
#include "jyt_avrcp.h"

/** AVRCP 当前媒体状态快照。 */
typedef struct {
    uint32_t track_uid_msb;                         /**< Track UID 高 32 位。 */
    uint32_t track_uid_lsb;                         /**< Track UID 低 32 位。 */
    char title[JYT_AVRCP_EVENT_TEXT_MAX_LEN + 1u];  /**< AVRCP Title 原始文本。 */
    char artist[JYT_AVRCP_EVENT_TEXT_MAX_LEN + 1u]; /**< AVRCP Artist 原始文本。 */
    uint32_t position_ms;                           /**< 当前播放位置，单位毫秒。 */
    uint32_t duration_ms;                           /**< 当前媒体总时长，单位毫秒。 */
    jyt_avrcp_play_status_t play_status;            /**< 当前播放状态。 */
    jyt_avrcp_progress_source_t progress_source;    /**< 最近一次进度的数据来源。 */
    bool track_valid;                               /**< Track UID 是否有效。 */
    bool play_status_valid;                         /**< 播放状态是否已由 OS 同步。 */
    bool progress_valid;                            /**< 播放位置和总时长是否有效。 */
} system_avrcp_snapshot_t;

/** AVRCP 状态变化监听函数。 */
typedef void (*system_avrcp_listener_t)(const system_avrcp_snapshot_t* snapshot);

/**
 * @brief 处理来自 OS 的 AVRCP 系统事件并更新缓存。
 * @param[in] msg 系统事件消息。
 * @return `true` 表示消息已识别，`false` 表示格式无效。
 */
bool system_avrcp_handle_event(const JYT_ELF_MQ_MSG* msg);

/**
 * @brief 获取当前 AVRCP 状态快照。
 * @return 只读状态指针，生命周期覆盖系统运行期。
 */
const system_avrcp_snapshot_t* system_avrcp_get_snapshot(void);

/**
 * @brief 设置当前页面的 AVRCP 状态监听函数，并立即同步已有状态。
 * @param[in] listener 监听函数；传 `NULL` 表示清除。
 * @return 无返回值。
 */
void system_avrcp_set_listener(system_avrcp_listener_t listener);

/**
 * @brief 仅当监听函数匹配时解除监听。
 * @param[in] listener 需要解除的监听函数。
 * @return 无返回值。
 */
void system_avrcp_clear_listener(system_avrcp_listener_t listener);

/**
 * @brief 亮屏后向已注册页面补发当前 AVRCP 状态。
 * @return 无返回值。
 */
void system_avrcp_flush_pending_after_screen_on(void);

/**
 * @brief 请求 OS 通过 AVRCP 控制当前音乐播放器。
 * @param[in] control 播放、暂停、切歌或音量调节动作。
 * @return `true` 表示请求已发出，`false` 表示动作无效。
 */
bool system_avrcp_send_control(jyt_avrcp_control_t control);
