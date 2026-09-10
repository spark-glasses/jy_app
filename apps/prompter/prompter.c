/**
 * @file prompter.c
 * @brief Prompter App 生命周期、消息注册和页面入口实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup app_prompter
 */
#include "prompter.h"
#include "common/app_framework/app_nav.h"
#include "message.h"
#include "app_def.h"
#include "floatair_fs.h"
#include "system/system.h"
#include "system/system_config_json.h"
#include <stdio.h>
#include <string.h>

extern void rpmsgttf_cache_bitmap_enable(bool enable);

#define PROMPTER_DEFAULT_REPORT_DOUBLE_CLICK false ///< 默认由眼镜端直接处理双击退出。

static app_message_t prompter_msg = {
    .id   = APP_MSG_ID_PROMPTER,
    .name = APP_NAME_PROMPTER,
    .cb   = prompter_route_cmd,
};

static bool s_prompter_msg_registered = false;    ///< Prompter 消息是否已注册
static bool s_prompter_report_double_click_enabled = PROMPTER_DEFAULT_REPORT_DOUBLE_CLICK; ///< 双击是否仅上报手机。

/**
 * @brief 从合并后的有效配置加载 Prompter 运行时开关。
 * @return 加载成功返回 `true`。
 */
bool prompter_config_load_runtime(void) {
    char config_path[SYSTEM_MAX_PATH_LEN] = {0};

    if (!floatair_fs_get_app_config_file(APP_NAME_PROMPTER, config_path, sizeof(config_path))) {
        return false;
    }
    cJSON* root = system_config_load_json(config_path);
    if (root == NULL) {
        return false;
    }
    s_prompter_report_double_click_enabled = cJSON_IsTrue(
        cJSON_GetObjectItemCaseSensitive(root, "reportdoubleclick"));
    cJSON_Delete(root);
    return true;
}

bool prompter_report_double_click_enabled(void) {
    return s_prompter_report_double_click_enabled;
}

/**
 * @brief 注册 Prompter 消息处理器。
 * @return `true` 表示注册成功，`false` 表示注册失败。
 */
static bool prompter_msg_register_once(void) {
    int ret = 0;

    if (s_prompter_msg_registered) {
        return true;
    }

    ret = app_msg_register(&prompter_msg);
    if (ret != 0) {
        return false;
    }
    s_prompter_msg_registered = true;
    return true;
}

/**
 * @brief Prompter App 启动。
 * @return 无返回值。
 */
static void prompter_app_on_start(void) {
    rpmsgttf_cache_bitmap_enable(true);

    if (!prompter_msg_register_once()) {
        floatair_assert(false, "app_msg_register failed");
        return;
    }

    if (!app_nav_replace((app_page_t*)prompter_page_get(), NULL, 0)) {
        floatair_assert(false, "prompter page replace failed");
    }
}

/**
 * @brief Prompter App 停止。
 * @return 无返回值。
 */
static void prompter_app_on_stop(void) {
    prompter_view_reset();
    rpmsgttf_cache_bitmap_enable(false);
}

static app_t s_prompter_app = {
    .name = APP_NAME_PROMPTER,
    .on_start = prompter_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = prompter_app_on_stop,
    .on_back = NULL,
};

bool prompter_app_register(void) {
    if (!prompter_msg_register_once()) {
        return false;
    }
    return app_manager_register(&s_prompter_app);
}
