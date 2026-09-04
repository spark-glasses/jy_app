/**
 * @file recorder_msg.c
 * @brief 录音应用 MsgPack 命令解析与路由实现。
 */
#include "recorder.h"

#include "common/app_framework/app_router.h"
#include "product_app.h"

#include <string.h>

/**
 * @brief 处理录音状态更新。
 * @param[in] node MsgPack 数据节点。
 * @param[in] msg 消息包。
 * @return `true` 表示响应发送成功，`false` 表示发送失败。
 */
static bool recorder_handle_set_state(mpack_node_t node, msg_pack_t* msg) {
    const char* current_app = app_router_get_app();
    uint32_t state = 0;

    if (current_app == NULL || strcmp(current_app, APP_NAME_RECORDER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u32(node, false, "state", &state) ||
        state > (uint32_t)RECORDER_STATE_RUNNING) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    recorder_set_state((recorder_state_t)state);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

/**
 * @brief 处理录音计时更新。
 * @param[in] node MsgPack 数据节点。
 * @param[in] msg 消息包。
 * @return `true` 表示响应发送成功，`false` 表示发送失败。
 */
static bool recorder_handle_set_tick(mpack_node_t node, msg_pack_t* msg) {
    const char* current_app = app_router_get_app();
    uint32_t tick = 0;

    if (current_app == NULL || strcmp(current_app, APP_NAME_RECORDER) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (!app_msg_get_u32(node, false, "tick", &tick)) {
        return app_mpack_send_ack(msg, ErrBadParam);
    }

    recorder_set_tick(tick);
    return app_mpack_send_ack(msg, Dp_ErrNone);
}

static app_cmd_func_t s_recorder_cmd_funcs[] = {
    {"setState", recorder_handle_set_state},
    {"setTick", recorder_handle_set_tick},
};

bool recorder_route_cmd(mpack_node_t node, msg_pack_t* msg) {
    if (msg == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(s_recorder_cmd_funcs) / sizeof(s_recorder_cmd_funcs[0]); ++i) {
        if (strcmp(msg->cmd, s_recorder_cmd_funcs[i].cmd) == 0) {
            return s_recorder_cmd_funcs[i].func(node, msg);
        }
    }
    return app_mpack_send_ack(msg, ErrCmdErr);
}
