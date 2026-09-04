#include "spark.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "common/message.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

static bool spark_message(mpack_node_t data, msg_pack_t* msg) {
    if (msg == NULL) return false;
    if (strcmp(msg->biz, "Spark") != 0) return app_mpack_send_ack(msg, ErrBizErr);
    if (strcmp(msg->cmd, "setReply") != 0) return app_mpack_send_ack(msg, ErrCmdErr);
    if (msg->type != MSG_TYPE_DATA_RELIABLE) return app_mpack_send_ack(msg, ErrTypeErr);
    if (app_manager_current_name() == NULL ||
        strcmp(app_manager_current_name(), APP_NAME_HOME) != 0) {
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    if (mpack_node_type(data) != mpack_type_map) return app_mpack_send_ack(msg, ErrBadParam);

    mpack_node_t text_node = mpack_node_map_cstr(data, "text");
    mpack_node_check_utf8_cstr(text_node);
    if (mpack_node_error(text_node) != mpack_ok) return app_mpack_send_ack(msg, ErrBadParam);
    size_t length = mpack_node_strlen(text_node);
    if (length >= UINT16_MAX) return app_mpack_send_ack(msg, ErrBadParam);

    char* text = malloc(length + 1);
    if (text == NULL) return app_mpack_send_ack(msg, ErrNotReady);
    mpack_node_copy_utf8_cstr(text_node, text, length + 1);
    bool applied = system_ui_set_reply(text);
    floatair_info("Spark reply seq=%lu text_bytes=%lu font=open-runde size=12 applied=%d",
                  (unsigned long)msg->sequence, (unsigned long)length,
                  applied);
    free(text); // The label owns a copy; the message tree is also temporary.
    return app_mpack_send_ack(msg, applied ? Dp_ErrNone : ErrNotReady);
}

static app_message_t s_spark_message = {
    .id = APP_MSG_ID_HOME,
    .name = "Spark",
    .cb = spark_message,
};

static void spark_app_on_pause(void) {
    (void)system_ui_set_reply("");
}

static void spark_app_on_stop(void) {
    (void)app_msg_delete(APP_MSG_ID_HOME);
    spark_app_on_pause();
}

static void spark_app_on_start(void) {
    int result = app_msg_register(&s_spark_message);
    floatair_assert(result == 0, "Spark message registration failed");
    system_status_bar_set_mode(true);
    if (!app_nav_replace(spark_page_get(), NULL, 0)) {
        floatair_assert(false, "Spark page replace failed");
    }
}

static app_t s_spark_app = {
    // Keep setup, reset, and return-home routes pointed at Spark.
    .name = APP_NAME_HOME,
    .on_start = spark_app_on_start,
    .on_pause = spark_app_on_pause,
    .on_stop = spark_app_on_stop,
};

bool spark_app_register(void) {
    return app_manager_register(&s_spark_app);
}
