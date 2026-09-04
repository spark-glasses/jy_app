/**
 * @file transcribe.c
 * @brief 转写 App 身份与 Speech 共享运行时配置。
 */
#include "transcribe.h"

#include "app_def.h"
#include "common/speech/speech_runtime.h"

static const speech_app_profile_t s_transcribe_profile = {
    .app_name = APP_NAME_TRANSCRIBE,
    .config_name = "transcribe",
    .msg_id = APP_MSG_ID_TRANSCRIBE,
    .exit_message_key = "TRANSCRIBE_EXIT_DIALOG_MESSAGE",
};

const product_app_module_t transcribe_app_module = {
    .register_profile = speech_runtime_register,
    .profile = &s_transcribe_profile,
};
