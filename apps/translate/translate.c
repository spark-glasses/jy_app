/**
 * @file translate.c
 * @brief 翻译 App 身份与 Speech 共享运行时配置。
 */
#include "translate.h"

#include "app_def.h"
#include "common/speech/speech_runtime.h"

static const speech_app_profile_t s_translate_profile = {
    .app_name = APP_NAME_TRANSLATE,
    .config_name = "translate",
    .msg_id = APP_MSG_ID_TRANSLATE,
    .exit_message_key = "TRANSLATE_EXIT_DIALOG_MESSAGE",
};

const product_app_module_t translate_app_module = {
    .register_profile = speech_runtime_register,
    .profile = &s_translate_profile,
};
