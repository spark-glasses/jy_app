/**
 * @file system_msg_request.c
 * @brief 系统模块主动请求系统管理器和底层服务。
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-22
 * @copyright JYTek
 * @ingroup app_system
 */

#include <time.h>

#include "elf_common.h"
#include "floatair_dbg.h"
#include "floatair_osal.h"
#include "system/system.h"

#include <stdint.h>

#include "floatair_def.h"

/**
 * @brief 向本地系统管理器发送消息请求。
 * @param[in] msg_id 本地消息 ID。
 * @param[in] data 消息数据缓冲区。
 * @param[in] len 消息数据长度。
 * @return `true` 表示请求已发出，`false` 表示请求失败。
 */
static bool system_request_msg(LOCAL_MSG_ID msg_id, const uint8_t* data, uint32_t len) {
    OSAL_INST_SEND_LOCAL_MQ_MSG(MQ_JYT_SYSTEM_MANAGER_DATA_IN, msg_id, data, len);
    return true;
}

/**
 * @brief 请求系统管理器返回最新设备状态。
 * @return `true` 表示请求已发出，`false` 表示请求失败。
 */
bool system_request_device_state(void) {
    floatair_info("request device state");
    return system_request_msg(LMID_SMMAN_GET_DEV_STATE, NULL, 0);
}

/**
 * @brief 请求系统管理器执行设备控制命令。
 * @param[in] cmd 设备控制命令。
 * @return `true` 表示请求已发出，`false` 表示请求失败。
 */
bool system_request_device_control(const dev_ctl_cmd_t* cmd) {
    if (!cmd) {
        floatair_err("device control cmd is NULL");
        return false;
    }

    floatair_info("request device control: type=%u code=%u data=%u",
                  (unsigned)cmd->dev_type,
                  (unsigned)cmd->control_code,
                  (unsigned)cmd->data);
    return system_request_msg(LMID_SMMAN_DEVCTL_REQ, (const uint8_t*)cmd, sizeof(*cmd));
}

/**
 * @brief 请求 OS 层切换系统休眠许可。
 * @param[in] enable `true` 表示允许休眠，`false` 表示禁止休眠。
 * @return `true` 表示请求已发送，`false` 表示请求失败。
 */
bool system_request_os_sleep(bool enable) {
    dev_ctl_cmd_t sleep_cmd = {
        .dev_type = DEV_SLEEP_CTRL,
        .control_code = enable ? SLEEP_SUB_ENABLE : SLEEP_SUB_DISABLE,
        .data = 0,
    };

    return system_request_device_control(&sleep_cmd);
}

/**
 * @brief 请求底层切换 KWS 关键词唤醒状态。
 * @param[in] enabled `true` 表示启用 KWS，`false` 表示禁用 KWS。
 * @return `true` 表示请求已发送，`false` 表示发送失败。
 */
bool system_request_keyword_spotting_enabled(bool enabled) {
    dev_ctl_cmd_t kws_cmd = {
        .dev_type = DEV_KWS_CTRL,
        .control_code = KWS_CTRL_SET,
        .data = enabled ? 1 : 0,
    };

    return system_request_device_control(&kws_cmd);
}

/**
 * @brief 请求底层更新抬头/低头 IMU 触发阈值。
 * @param[in] heads_up_threshold 抬头触发 pitch 阈值。
 * @param[in] heads_down_threshold 低头触发 pitch 阈值。
 * @return `true` 表示请求已发送，`false` 表示请求失败。
 */
bool system_request_imu_threshold(float heads_up_threshold, float heads_down_threshold) {
    struct {
        JDB_OP_CMD cmd;
        JDB_OP_PDU_IMU_THRESHOLD pdu;
    } req = {
        .cmd = SET_IMU_THRESHOLD,
        .pdu = {
            .heads_up_threshold = heads_up_threshold,
            .heads_down_threshold = heads_down_threshold,
        },
    };

    floatair_info("request imu threshold: up=%.2f down=%.2f",
                  (double)heads_up_threshold,
                  (double)heads_down_threshold);
    return system_request_msg(LMID_SMMAN_JDB_OP, (const uint8_t*)&req, sizeof(req));
}

/**
 * @brief 将 app 配置中需要底层感知的部分同步到底层。
 * @return 无返回值。
 */
void system_sync_config_to_device(void) {
    system_head_gesture_config_t head_gesture = {0};
    if (system_config_get_head_gesture_config(&head_gesture)) {
        float heads_up_threshold = (float)(head_gesture.base_deg + head_gesture.up_deg);
        float heads_down_threshold = (float)(head_gesture.base_deg - head_gesture.down_deg);
        (void)system_request_imu_threshold(heads_up_threshold, heads_down_threshold);
    }

    dev_ctl_cmd_t wearing_cmd = {
        .dev_type = DEV_WEARING_CTRL,
        .control_code = system_config_get_wear_detection_enabled() ? 1 : 0,
        .data = 0,
    };
    (void)system_request_device_control(&wearing_cmd);
}

/**
 * @brief 请求 BTH helper 切换蓝牙可见性。
 * @param[in] visibility 蓝牙可见性，取值见 `SMMAN_BT_VISIBILITY`。
 * @return `true` 表示请求已发出，`false` 表示请求失败。
 */
bool system_request_bt_visibility(uint8_t visibility) {
    dev_ctl_cmd_t cmd = {
        .dev_type = DEV_BT_VISIBLE_CTRL,
        .control_code = visibility,
        .data = 0,
    };

    floatair_info("request bt visibility %u", visibility);
    return system_request_device_control(&cmd);
}

/**
 * @brief 请求 OS 持久化恢复出厂标记、清除配对信息并协调重启。
 * @return `true` 表示请求已发出，`false` 表示请求失败。
 */
bool system_request_factory_reset(void) {
    dev_ctl_cmd_t cmd = {
        .dev_type = DEV_FACTORY_RESET,
        .control_code = 0,
        .data = 0,
    };

    floatair_info("request factory reset");
    return system_request_device_control(&cmd);
}

/**
 * @brief 周期向底层请求最新设备时间，由返回消息统一刷新状态栏。
 * @return 无返回值。
 */
void system_update_time(void) {
    (void)system_request_device_state();
}
