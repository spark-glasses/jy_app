/**
 * @file system_msg_tap_msgbox.c
 * @brief TapMsgbox 手机协议处理实现。
 */
#include "message.h"
#include "common/widgets/msgbox.h"

#define SYSTEM_TAP_MSGBOX_TEXT_MAX_LEN MSG_STR_MAX_LEN

typedef enum {
    SYSTEM_TAP_MSGBOX_NONE = 0,
    SYSTEM_TAP_MSGBOX_NORMAL,
    SYSTEM_TAP_MSGBOX_DOWNLOAD_PROGRESS,
} system_tap_msgbox_type_t;

static system_tap_msgbox_type_t s_active_tap_msgbox_type = SYSTEM_TAP_MSGBOX_NONE;

/**
 * @brief 处理 TapMsgbox.showTapMsgbox 命令。
 *
 * @param[in] node 消息 data 节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool system_tap_msgbox_show(mpack_node_t node, msg_pack_t* msg) {
    char title[SYSTEM_TAP_MSGBOX_TEXT_MAX_LEN] = {0};
    char hint[SYSTEM_TAP_MSGBOX_TEXT_MAX_LEN] = {0};

    if (msg == NULL) {
        return false;
    }
    if (!msgbox_remote_supported()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_str(node, "title", title, sizeof(title)) ||
        !app_msg_get_str(node, "hint", hint, sizeof(hint))) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!msgbox_show_remote(title, hint)) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    s_active_tap_msgbox_type = SYSTEM_TAP_MSGBOX_NORMAL;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理 TapMsgbox.showDownloadProgress 命令。
 *
 * 手机端在进度变化时重复下发完整当前值，眼镜端只原地刷新。
 *
 * @param[in] node 消息 data 节点。
 * @param[in,out] msg 消息上下文。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool system_tap_msgbox_show_download_progress(mpack_node_t node,
                                                     msg_pack_t* msg) {
    char title[SYSTEM_TAP_MSGBOX_TEXT_MAX_LEN] = {0};
    uint8_t progress = 0;

    if (msg == NULL) {
        return false;
    }
    if (!msgbox_remote_supported()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_str(node, "title", title, sizeof(title)) ||
        !app_msg_get_u8(node, false, "progress", &progress) || progress > 100) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!msgbox_show_remote_progress(title, progress)) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    s_active_tap_msgbox_type = SYSTEM_TAP_MSGBOX_DOWNLOAD_PROGRESS;
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 关闭指定协议入口创建的 TapMsgbox。
 *
 * @param[in,out] msg 消息上下文。
 * @param[in] type 要关闭的入口类型。
 * @return `true` 表示处理成功，`false` 表示处理失败。
 */
static bool system_tap_msgbox_close_type(msg_pack_t* msg,
                                         system_tap_msgbox_type_t type) {
    if (msg == NULL) {
        return false;
    }
    if (!msgbox_remote_supported()) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (s_active_tap_msgbox_type == type) {
        msgbox_dismiss_remote();
        s_active_tap_msgbox_type = SYSTEM_TAP_MSGBOX_NONE;
    }
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static bool system_tap_msgbox_close(mpack_node_t node, msg_pack_t* msg) {
    (void)node;
    return system_tap_msgbox_close_type(msg, SYSTEM_TAP_MSGBOX_NORMAL);
}

static bool system_tap_msgbox_close_download_progress(mpack_node_t node,
                                                      msg_pack_t* msg) {
    (void)node;
    return system_tap_msgbox_close_type(msg,
                                        SYSTEM_TAP_MSGBOX_DOWNLOAD_PROGRESS);
}

app_cmd_func_t system_tap_msgbox_cmd_funcs[] = {
    {"showTapMsgbox", system_tap_msgbox_show},
    {"showDownloadProgress", system_tap_msgbox_show_download_progress},
    {"closeTapMsgbox", system_tap_msgbox_close},
    {"closeDownloadProgress", system_tap_msgbox_close_download_progress},
};
const size_t system_tap_msgbox_cmd_funcs_count =
    sizeof(system_tap_msgbox_cmd_funcs) / sizeof(system_tap_msgbox_cmd_funcs[0]);
