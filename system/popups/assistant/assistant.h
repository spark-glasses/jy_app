/**
 * @file assistant.h
 * @brief Assistant 弹窗模块公共接口声明。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "message.h"
#include <lvgl/lvgl.h>

/**
 * @brief 打开 assistant 弹窗层。
 * @return `true` 表示打开成功，`false` 表示打开失败。
 */
bool assistant_open(void);
/**
 * @brief Roll out the avatar, then close the popup and release its resources.
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` when close is accepted, `false` on failure.
 */
bool assistant_close(bool report_close);
/**
 * @brief Check whether the popup exists, including during roll-out.
 * @return `true` 表示正在显示，`false` 表示未显示。
 */
bool assistant_is_open(void);
/**
 * @brief Set whether the assistant avatar shows the listening state.
 * @param[in] listening `true` starts listening animation; `false` restores idle.
 * @return No return value.
 */
void assistant_set_listening(bool listening);
/**
 * @brief 处理 assistant 弹窗输入事件。
 * @param[in] code LVGL 事件码。
 * @return `true` 表示事件已被 assistant 弹窗消费，`false` 表示未消费。
 */
bool assistant_handle_event(lv_event_code_t code);

/**
 * @brief 处理 SystemControl.openAssistant 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_open_cmd(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief Handle SystemControl.setAssistantState: 0 is normal, 1 is listening.
 * @param[in] node Message data with the required state field.
 * @param[in,out] msg Message context used for the acknowledgement.
 * @return Result of sending the acknowledgement.
 */
bool assistant_set_state_cmd(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief 处理 SystemControl.updateAssistantSttInfo 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_update_stt_info_cmd(mpack_node_t node, msg_pack_t* msg);
/**
 * @brief 处理 SystemControl.closeAssistant 命令。
 * @param[in] node 消息 payload 数据节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
bool assistant_close_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 在 popup 层创建 assistant 视图。
 * @return `true` 表示创建成功，`false` 表示创建失败。
 */
bool assistant_popup_open(void);
/**
 * @brief Roll out the avatar, then delete the assistant popup.
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return `true` when close is accepted or the popup is already absent.
 */
bool assistant_popup_close(bool report_close);
/**
 * @brief 通知 assistant popup 已被删除。
 *
 * 用于 popup 被外部对象树删除时补做生命周期清理，确保 STT 快照能恢复。
 *
 * @param[in] report_close 是否主动上报 assistant 已关闭。
 * @return 无返回值。
 */
void assistant_on_popup_deleted(bool report_close);

void assistant_stt_update(void);
/**
 * @brief 记录最近一次 assistant 文本更新的协议字段，用于区分当前问答区域。
 * @param[in] area 文本区域。
 * @return 无返回值。
 */
void assistant_stt_note_update(uint8_t area);
void assistant_stt_clear(void);

#ifdef __cplusplus
}
#endif
