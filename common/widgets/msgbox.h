/**
 * @file msgbox.h
 * @brief 产品选择的统一消息确认框接口。
 */
#ifndef COMMON_WIDGETS_MSGBOX_H
#define COMMON_WIDGETS_MSGBOX_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 本地确认框选择结果。
 */
typedef enum {
    MSGBOX_CHOICE_CANCEL = 0,  ///< 取消当前操作。
    MSGBOX_CHOICE_CONFIRM = 1, ///< 确认当前操作。
} msgbox_choice_t;

/**
 * @brief 本地确认框结果回调。
 * @param[in] choice 用户选择。
 * @param[in] user_data 调用方透传数据。
 * @return 无返回值。
 */
typedef void (*msgbox_choice_cb_t)(msgbox_choice_t choice, void* user_data);

/**
 * @brief 消息框输入处理结果。
 */
typedef enum {
    MSGBOX_EVENT_IGNORED = 0,         ///< 当前没有活动消息框，事件未消费。
    MSGBOX_EVENT_CONSUMED_REPORT = 1, ///< 事件已消费，并需要上报手机。
    MSGBOX_EVENT_CONSUMED_LOCAL = 2,  ///< 事件已消费，并已在本地处理。
} msgbox_event_result_t;

/**
 * @brief 显示产品选定样式的本地确认框。
 *
 * Jytek 使用 Classic 实现显示左右按钮。
 *
 * @param[in] title 确认内容。
 * @param[in] cancel_action 取消操作文案。
 * @param[in] confirm_action 确认操作文案。
 * @param[in] choice_cb 结果回调；不可为空。
 * @param[in] user_data 调用方透传数据。
 * @return 成功返回 `true`，失败返回 `false`。
 */
bool msgbox_show_local(const char* title,
                       const char* cancel_action,
                       const char* confirm_action,
                       msgbox_choice_cb_t choice_cb,
                       void* user_data);

/**
 * @brief 仅关闭当前本地确认框。
 * @return 无返回值。
 */
void msgbox_dismiss_local(void);

/**
 * @brief 判断本地确认框是否正在显示。
 * @return 正在显示返回 `true`，否则返回 `false`。
 */
bool msgbox_is_local_active(void);

/**
 * @brief 判断当前产品的 MsgBox 实现是否支持手机 TapMsgbox 协议。
 * @return 支持返回 `true`，否则返回 `false`。
 */
bool msgbox_remote_supported(void);

/**
 * @brief 显示手机协议下发的 TapMsgBox。
 * @param[in] title 第一行正文。
 * @param[in] hint 第二行操作提示。
 * @return 当前实现支持且显示成功返回 `true`，否则返回 `false`。
 */
bool msgbox_show_remote(const char* title, const char* hint);

/**
 * @brief 显示手机协议下发的下载进度框。
 * @param[in] title 第一行正文。
 * @param[in] progress 进度值，范围 0-100。
 * @return 当前实现支持且显示成功返回 `true`，否则返回 `false`。
 */
bool msgbox_show_remote_progress(const char* title, uint8_t progress);

/**
 * @brief 仅关闭当前手机协议消息框。
 * @return 无返回值。
 */
void msgbox_dismiss_remote(void);

/**
 * @brief 处理当前消息框的输入事件。
 * @param[in] code LVGL 事件码。
 * @return 返回事件未消费、需上报手机或已在本地处理。
 */
msgbox_event_result_t msgbox_handle_active_event(lv_event_code_t code);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_WIDGETS_MSGBOX_H */
