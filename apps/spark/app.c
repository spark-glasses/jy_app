#include "spark.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "common/message.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"

static uint64_t s_revision;
static bool s_has_revision;
static uint32_t s_report_sequence;
static uint64_t s_navigation_sequence;
static char s_display_id[65];
static bool s_last_page_skipped;
static spark_assistant_presentation_t s_assistant = {
    .state = SPARK_ASSISTANT_IDLE,
};

void spark_display_reset_revision(void) {
    s_has_revision = false;
    s_display_id[0] = '\0';
    s_navigation_sequence = 0;
    s_last_page_skipped = false;
    s_assistant.state = SPARK_ASSISTANT_IDLE;
    s_assistant.detail[0] = '\0';
}

const spark_assistant_presentation_t* spark_assistant_current(void) { return &s_assistant; }

static bool spark_node_is(mpack_node_t node, const char* value) {
    size_t length = strlen(value);
    return mpack_node_type(node) == mpack_type_str && mpack_node_strlen(node) == length &&
           memcmp(mpack_node_str(node), value, length) == 0;
}

static bool spark_assistant_parse(
    mpack_node_t node, spark_assistant_presentation_t* presentation) {
    if (presentation == NULL || mpack_node_type(node) != mpack_type_map) return false;
    mpack_node_t state = mpack_node_map_cstr(node, "state");
    if (spark_node_is(state, "idle")) presentation->state = SPARK_ASSISTANT_IDLE;
    else if (spark_node_is(state, "listening")) presentation->state = SPARK_ASSISTANT_LISTENING;
    else if (spark_node_is(state, "thinking")) presentation->state = SPARK_ASSISTANT_THINKING;
    else if (spark_node_is(state, "working")) presentation->state = SPARK_ASSISTANT_WORKING;
    else if (spark_node_is(state, "notes")) presentation->state = SPARK_ASSISTANT_NOTES;
    else if (spark_node_is(state, "todo")) presentation->state = SPARK_ASSISTANT_TODO;
    else if (spark_node_is(state, "calendar")) presentation->state = SPARK_ASSISTANT_CALENDAR;
    else if (spark_node_is(state, "email")) presentation->state = SPARK_ASSISTANT_EMAIL;
    else if (spark_node_is(state, "contacts")) presentation->state = SPARK_ASSISTANT_CONTACTS;
    else if (spark_node_is(state, "maps")) presentation->state = SPARK_ASSISTANT_MAPS;
    else if (spark_node_is(state, "web")) presentation->state = SPARK_ASSISTANT_WEB;
    else if (spark_node_is(state, "error")) presentation->state = SPARK_ASSISTANT_ERROR;
    else if (mpack_node_type(state) == mpack_type_str) presentation->state = SPARK_ASSISTANT_WORKING;
    else return false;
    presentation->detail[0] = '\0';
    if (mpack_node_map_contains_cstr(node, "detail")) {
        mpack_node_copy_utf8_cstr(
            mpack_node_map_cstr(node, "detail"), presentation->detail,
            sizeof(presentation->detail));
    }
    return mpack_node_error(node) == mpack_ok;
}

static void write_revision(mpack_writer_t* writer, uint64_t revision) {
    char text[21];
    snprintf(text, sizeof(text), "%llu", (unsigned long long)revision);
    mpack_write_cstr(writer, text);
}

static bool spark_ack_revision(msg_pack_t* msg) {
    msg_pack_writer_t* writer = app_mpack_create_writer(msg, MSG_TYPE_ACK);
    if (writer == NULL) return false;
    mpack_start_map(&writer->writer, 1);
    mpack_write_cstr(&writer->writer, "batch_id");
    write_revision(&writer->writer, s_revision);
    mpack_finish_map(&writer->writer);
    return app_mpack_send_writer(writer);
}

void spark_display_report(const char* command, const char* artifact_id) {
    if (!s_has_revision) return;
    ++s_navigation_sequence;
    // Matches VenusDisplayReportRoute.appID in the phone SDK adapter.
    msg_pack_t msg = {.id = 30003, .sequence = ++s_report_sequence};
    snprintf(msg.biz, sizeof(msg.biz), "Display");
    snprintf(msg.cmd, sizeof(msg.cmd), "%s", command);
    msg_pack_writer_t* writer = app_mpack_create_writer(&msg, MSG_TYPE_DATA_UNRELIABLE);
    if (writer == NULL) return;
    mpack_start_map(&writer->writer, artifact_id == NULL ? 3 : 4);
    mpack_write_cstr(&writer->writer, "revision");
    write_revision(&writer->writer, s_revision);
    mpack_write_cstr(&writer->writer, "displayID");
    mpack_write_cstr(&writer->writer, s_display_id);
    mpack_write_cstr(&writer->writer, "navigationSequence");
    write_revision(&writer->writer, s_navigation_sequence);
    if (artifact_id != NULL) {
        mpack_write_cstr(&writer->writer, "artifact_id");
        mpack_write_cstr(&writer->writer, artifact_id);
    }
    mpack_finish_map(&writer->writer);
    (void)app_mpack_send_writer(writer);
}

static bool ack_current(msg_pack_t* msg) {
    bool acked = spark_ack_revision(msg);
    if (s_last_page_skipped) {
        const spark_display_t* current = spark_display_current();
        if (current != NULL && current->count != 0)
            spark_display_report(current->is_list ? "selected" : "opened", current->rows[current->selected].id);
    }
    return acked;
}

static bool spark_message(mpack_node_t data, msg_pack_t* msg) {
    if (msg == NULL) return false;
    if (msg->type != MSG_TYPE_DATA_RELIABLE) return app_mpack_send_ack(msg, ErrTypeErr);
    const char* app = app_manager_current_name();
    if (app == NULL || strcmp(app, APP_NAME_HOME) != 0 || !spark_display_ready())
        return app_mpack_send_ack(msg, ErrNotReady);
    if (strcmp(msg->biz, "Display") != 0) return app_mpack_send_ack(msg, ErrBizErr);
    if (strcmp(msg->cmd, "update") != 0) return app_mpack_send_ack(msg, ErrCmdErr);
    uint64_t revision;
    if (!spark_display_read_revision(data, &revision)) return app_mpack_send_ack(msg, ErrBadParam);
    // Revisions identify immutable requests. Retries do not parse, allocate, or draw again.
    if (s_has_revision && revision == s_revision) return ack_current(msg);
    if (s_has_revision && revision < s_revision) return app_mpack_send_ack(msg, ErrSeqErr);
    if (mpack_tree_size(data.tree) > SPARK_DISPLAY_MAX_REQUEST_BYTES)
        return app_mpack_send_ack(msg, ErrBadParam);
    char display_id[65] = "";
    if (mpack_node_map_contains_cstr(data, "displayID"))
        mpack_node_copy_utf8_cstr(mpack_node_map_cstr(data, "displayID"), display_id, sizeof(display_id));
    uint64_t navigation_sequence = 0;
    if (mpack_node_map_contains_cstr(data, "navigationSequence") &&
        !spark_display_read_counter(data, "navigationSequence", &navigation_sequence))
        return app_mpack_send_ack(msg, ErrBadParam);
    bool new_display = strcmp(display_id, s_display_id) != 0;
    mpack_node_t page = mpack_node_map_cstr_optional(data, "page");
    mpack_node_t reply = mpack_node_map_cstr_optional(data, "reply");
    mpack_node_t assistant = mpack_node_map_cstr_optional(data, "assistant");
    bool has_page = !mpack_node_is_missing(page);
    bool has_reply = !mpack_node_is_missing(reply);
    bool has_assistant = !mpack_node_is_missing(assistant);
    if (!has_page && !has_reply && !has_assistant)
        return app_mpack_send_ack(msg, ErrBadParam);
    if (new_display && !has_page) return app_mpack_send_ack(msg, ErrBadParam);
    // A swipe or Back can overtake the phone's detail response. Never undo it.
    bool stale_navigation = has_page && !new_display && display_id[0] != '\0' &&
        navigation_sequence < s_navigation_sequence;
    spark_assistant_presentation_t next_assistant = s_assistant;
    if (has_assistant && !spark_assistant_parse(assistant, &next_assistant))
        return app_mpack_send_ack(msg, ErrBadParam);
    spark_display_t* next = has_page ? spark_display_parse(page) : NULL;
    char* text = has_reply ? mpack_node_utf8_cstr_alloc(reply, SPARK_DISPLAY_MAX_REPLY + 1) : NULL;
    if ((has_page && next == NULL) || (has_reply && text == NULL) || mpack_node_error(data) != mpack_ok) {
        spark_display_free(next);
        free(text);
        return app_mpack_send_ack(msg, ErrBadParam);
    }
    if (!stale_navigation && !new_display && display_id[0] != '\0' && next != NULL && !next->is_list &&
        !spark_display_is_selected(next->rows[0].id)) {
        spark_display_free(next);
        free(text);
        return app_mpack_send_ack(msg, ErrNotReady);
    }
    // The UI loop owns both updates; validation completes before either becomes visible.
    bool applied = !has_reply || spark_reply_set(text);
    if (applied && has_page && !stale_navigation)
        applied = spark_display_apply(next, new_display || display_id[0] == '\0');
    if (applied && has_assistant) applied = spark_assistant_apply(&next_assistant);
    if (!applied || stale_navigation) spark_display_free(next);
    free(text);
    if (!applied) return app_mpack_send_ack(msg, ErrNotReady);
    if (has_assistant) s_assistant = next_assistant;
    s_revision = revision;
    s_has_revision = true;
    s_last_page_skipped = stale_navigation;
    if (new_display) {
        strcpy(s_display_id, display_id);
        s_navigation_sequence = navigation_sequence;
    }
    return ack_current(msg);
}

static app_message_t s_spark_message = {
    .id = APP_MSG_ID_HOME,
    .name = "Spark",
    .cb = spark_message,
};

bool spark_display_preview(mpack_node_t data) {
    msg_pack_t msg = {.id = APP_MSG_ID_HOME, .type = MSG_TYPE_DATA_RELIABLE};
    strcpy(msg.biz, "Display");
    strcpy(msg.cmd, "update");
    (void)spark_message(data, &msg);
    uint64_t revision;
    return spark_display_read_revision(data, &revision) && s_has_revision && s_revision == revision;
}

static void spark_app_on_pause(void) {
    spark_display_clear();
}

static void spark_app_on_stop(void) {
    (void)app_msg_delete(APP_MSG_ID_HOME);
    spark_app_on_pause();
}

static void spark_app_on_start(void) {
    int result = app_msg_register(&s_spark_message);
    floatair_assert(result == 0, "Spark message registration failed");
    system_status_bar_set_mode_at(true, STATUS_BAR_POS_TOP);
    if (!app_nav_replace(spark_page_get(), NULL, 0)) {
        floatair_assert(false, "Spark page replace failed");
    }
}

static app_t s_spark_app = {
    .name = APP_NAME_HOME,
    .on_start = spark_app_on_start,
    .on_pause = spark_app_on_pause,
    .on_stop = spark_app_on_stop,
};

bool spark_app_register(void) {
    return app_manager_register(&s_spark_app);
}
