/**
 * @file recorder.h
 * @brief 录音应用公共状态与消息接口声明。
 */
#pragma once

#include "message.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 录音运行状态。
 */
typedef enum {
    RECORDER_STATE_PAUSE = 0,  ///< 暂停态，显示三角图标。
    RECORDER_STATE_RUNNING = 1 ///< 运行态，显示闪烁圆点。
} recorder_state_t;

/**
 * @brief 注册录音应用及其消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
bool recorder_app_register(void);

/**
 * @brief 路由录音业务命令。
 * @param[in] node MsgPack 数据节点。
 * @param[in] msg 消息包。
 * @return `true` 表示响应发送成功，`false` 表示发送失败。
 */
bool recorder_route_cmd(mpack_node_t node, msg_pack_t* msg);

/**
 * @brief 设置录音运行状态。
 * @param[in] state Pause 或 Running 状态。
 * @return 无返回值。
 */
void recorder_set_state(recorder_state_t state);

/**
 * @brief 设置录音计时秒数。
 * @param[in] tick_seconds 录音计时秒数。
 * @return 无返回值。
 */
void recorder_set_tick(uint32_t tick_seconds);

#ifdef __cplusplus
}
#endif
