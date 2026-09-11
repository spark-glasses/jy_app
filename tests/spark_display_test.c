#include "apps/spark/spark.h"
#include "system/system_runtime_ui.h"
#include "system/system_res.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static app_t* app;
static app_message_t* receiver;
static lv_obj_t* parent;
static unsigned frames, flushed_pixels;
static MsgDpErr last_error;
static char last_revision[21], last_command[32];
static uint8_t outgoing_type;
static const char* active_app = "home";
static uint16_t screen_pixels[540 * 440];
static unsigned test_selected;
static spark_row_layout_t test_layout = SPARK_LAYOUT_REMINDER;
static bool test_mixed;
static const char* test_display_id = "";
static uint64_t test_navigation_sequence, last_navigation_sequence;
static char last_artifact_id[257], last_display_id[65];
static bool drop_report;
static const char* test_assistant_state;
static const char* test_assistant_detail;

bool app_manager_register(app_t* value) { app = value; return true; }
const char* app_manager_current_name(void) { return active_app; }
const char* app_router_get_app(void) { return active_app; }
int app_msg_register(app_message_t* value) { receiver = value; return 0; }
int app_msg_delete(uint32_t id) { (void)id; receiver = NULL; return 0; }
bool app_nav_replace(app_page_t* page, const void* data, size_t size) {
    (void)data; (void)size; page->on_create(parent, NULL); return true;
}
bool system_ui_request_screen_refresh(void) {
    ++frames;
    return true;
}
void system_status_bar_set_mode_at(bool visible, status_bar_widget_pos_t pos) {
    (void)visible;
    (void)pos;
}
const lv_font_t* get_font_by_size_near(uint32_t size) { return size >= 24 ? &lv_font_montserrat_24 : &lv_font_montserrat_18; }
bool app_mpack_send_ack(msg_pack_t* msg, MsgDpErr error) {
    assert(msg->id == APP_MSG_ID_HOME);
    last_error = error; return true;
}
msg_pack_writer_t* app_mpack_create_writer(msg_pack_t* msg, uint8_t type) {
    assert(msg->id == (type == MSG_TYPE_ACK ? APP_MSG_ID_HOME : 30003));
    assert(strcmp(msg->biz, "Display") == 0);
    outgoing_type = type;
    if (type != MSG_TYPE_ACK && drop_report) return NULL;
    snprintf(last_command, sizeof(last_command), "%s", msg->cmd);
    msg_pack_writer_t* writer = calloc(1, sizeof(*writer));
    assert(writer != NULL);
    mpack_writer_init_growable(&writer->writer, &writer->buffer, &writer->size);
    return writer;
}
bool app_mpack_send_writer(msg_pack_writer_t* writer) {
    assert(mpack_writer_destroy(&writer->writer) == mpack_ok);
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, writer->buffer, writer->size);
    mpack_tree_parse(&tree);
    mpack_node_t revision = mpack_node_map_cstr(mpack_tree_root(&tree),
        outgoing_type == MSG_TYPE_ACK ? "batch_id" : "revision");
    mpack_node_copy_utf8_cstr(revision, last_revision, sizeof(last_revision));
    if (outgoing_type != MSG_TYPE_ACK) {
        mpack_node_t root = mpack_tree_root(&tree);
        assert(spark_display_read_counter(root, "navigationSequence", &last_navigation_sequence));
        mpack_node_copy_utf8_cstr(mpack_node_map_cstr(root, "displayID"), last_display_id, sizeof(last_display_id));
        if (mpack_node_map_contains_cstr(root, "artifact_id"))
            mpack_node_copy_utf8_cstr(mpack_node_map_cstr(root, "artifact_id"), last_artifact_id, sizeof(last_artifact_id));
    }
    assert(mpack_tree_error(&tree) == mpack_ok);
    last_error = Dp_ErrNone;
    mpack_tree_destroy(&tree);
    free(writer->buffer); free(writer); return true;
}
static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    size_t width = (size_t)lv_area_get_width(area);
    flushed_pixels += width * lv_area_get_height(area);
    for (int32_t y = area->y1; y <= area->y2; ++y)
        memcpy(screen_pixels + y * 540 + area->x1,
               pixels + (y - area->y1) * width * sizeof(uint16_t), width * sizeof(uint16_t));
    lv_display_flush_ready(display);
}

static void text(mpack_writer_t* writer, const char* key, const char* value) {
    mpack_write_cstr(writer, key); mpack_write_cstr(writer, value);
}

// count < 0 omits the page. A NULL reply omits the footer update.
static void send_request(const char* revision, int count, bool list, const char* reply, unsigned invalid) {
    char* bytes = NULL; size_t size = 0;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, &bytes, &size);
    mpack_start_map(&writer, 3 + (count >= 0) + (reply != NULL) +
                                 (test_assistant_state != NULL));
    text(&writer, "revision", revision);
    text(&writer, "displayID", test_display_id);
    char sequence[21]; snprintf(sequence, sizeof(sequence), "%llu", (unsigned long long)test_navigation_sequence);
    text(&writer, "navigationSequence", sequence);
    if (reply != NULL) text(&writer, "reply", reply);
    if (test_assistant_state != NULL) {
        mpack_write_cstr(&writer, "assistant");
        mpack_start_map(&writer, 1 + (test_assistant_detail != NULL));
        text(&writer, "state", test_assistant_state);
        if (test_assistant_detail != NULL) text(&writer, "detail", test_assistant_detail);
        mpack_finish_map(&writer);
    }
    if (count >= 0) {
        mpack_write_cstr(&writer, "page"); mpack_start_map(&writer, list && count ? 6 : 5);
        text(&writer, "kind", list ? "list" : "item");
        text(&writer, "title", "Items"); text(&writer, "hint", "8 items");
        if (list && count) {
            mpack_write_cstr(&writer, "rowGap"); mpack_write_u32(&writer, invalid == 9 ? 0 : SPARK_DISPLAY_ROW_GAP);
        }
        mpack_write_cstr(&writer, "selected"); mpack_write_u32(&writer, invalid == 3 ? 10 : test_selected);
        mpack_write_cstr(&writer, "rows"); mpack_start_array(&writer, count);
        for (int i = 0; i < count; ++i) {
            char id[32]; snprintf(id, sizeof(id), "item-%d", invalid == 1 ? 0 : i);
            spark_row_layout_t layout = test_mixed ? SPARK_LAYOUT_NOTE + i : test_layout;
            const char* names[] = {"detail", "reminder", "note", "email", "event"};
            bool email = layout == SPARK_LAYOUT_EMAIL && list;
            mpack_start_map(&writer, list ? (email ? 9 : 7) : 5);
            if (list) {
                text(&writer, "layout", invalid == 7 ? "bad" : names[layout]);
                mpack_write_cstr(&writer, "height");
                mpack_write_u32(&writer, invalid == 6 ? 500 : layout == SPARK_LAYOUT_REMINDER ? 40 : layout == SPARK_LAYOUT_EVENT ? 88 : 64);
                if (email) {
                    text(&writer, "address", "dr.okafor@example.com");
                    text(&writer, "subject", invalid == 10 ? "Wrong prefix" : "Scan results");
                }
            }
            text(&writer, "id", id); text(&writer, "mark", "[ ]");
            text(&writer, "primary", list ? (layout == SPARK_LAYOUT_EMAIL ? "Dr Okafor" : layout == SPARK_LAYOUT_EVENT ? "Studio crit" : layout == SPARK_LAYOUT_NOTE ? "Meeting notes" : "Call Kim back") : "A long lead that wraps across several lines on this display. A long lead that wraps across several lines on this display. A long lead that wraps across several lines on this display.");
            mpack_write_cstr(&writer, "secondary");
            if (invalid == 2) mpack_write_str(&writer, "bad\0text", 8);
            else if (invalid == 4) mpack_write_str(&writer, "\xff", 1);
            else if (invalid == 5 || invalid == 12) {
                char huge[SPARK_DISPLAY_MAX_TEXT + 1]; memset(huge, 'a', sizeof(huge));
                mpack_write_str(&writer, huge, invalid == 5 ? sizeof(huge) : SPARK_DISPLAY_MAX_LIST_TEXT + 1);
            } else mpack_write_cstr(&writer, email ? "Scan results - Sending the MRI from Tuesday. Please review before Thursday." : layout == SPARK_LAYOUT_EVENT && list ? "Sep 5, 2026, 10:00 AM - 11:00 AM" : "Body content");
            text(&writer, "meta", email ? "10:43" : layout == SPARK_LAYOUT_EVENT && list ? "Office, Room 3" : "Detail"); mpack_finish_map(&writer);
        }
        mpack_finish_array(&writer); mpack_finish_map(&writer);
    }
    mpack_finish_map(&writer); assert(mpack_writer_destroy(&writer) == mpack_ok);
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, bytes, size); mpack_tree_parse(&tree);
    msg_pack_t msg = {.id = APP_MSG_ID_HOME, .sequence = 42, .type = MSG_TYPE_DATA_RELIABLE};
    strcpy(msg.biz, "Display"); strcpy(msg.cmd, "update");
    assert(receiver->cb(mpack_tree_root(&tree), &msg));
    mpack_tree_destroy(&tree); free(bytes);
}

static void save_frame(const char* directory, const char* name) {
    if (directory == NULL) return;
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE* file = fopen(path, "wb"); assert(file != NULL);
    fprintf(file, "P6\n540 440\n255\n");
    for (size_t i = 0; i < 540 * 440; ++i) {
        uint16_t pixel = screen_pixels[i];
        unsigned char rgb[] = {((pixel >> 11) & 31) * 255 / 31,
                               ((pixel >> 5) & 63) * 255 / 63, (pixel & 31) * 255 / 31};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

int main(int argc, char** argv) {
    lv_init();
    lv_display_t* display = lv_display_create(540, 440);
    static uint16_t pixels[540 * 40];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent); lv_obj_set_size(parent, 540, 390); lv_obj_set_pos(parent, 0, 50);
    assert(spark_app_register()); app->on_start();
    lv_obj_t* footer = lv_obj_get_child(parent, 1);
    lv_obj_t* avatar = lv_obj_get_child(footer, 0);
    lv_obj_t* avatar_image = lv_obj_get_child(avatar, 0);
    const void* first_avatar_frame = lv_image_get_src(avatar_image);
    int32_t first_avatar_x = lv_obj_get_style_translate_x(avatar, LV_PART_MAIN);
    lv_tick_inc(225); lv_anim_refr_now();
    assert(lv_image_get_src(avatar_image) != first_avatar_frame);
    assert(lv_obj_get_style_translate_x(avatar, LV_PART_MAIN) > first_avatar_x);
    lv_tick_inc(275); lv_anim_refr_now();
    assert(lv_obj_get_style_translate_x(avatar, LV_PART_MAIN) == 0);
    lv_obj_t* frame = lv_obj_get_child(parent, 0);
    lv_obj_t* reply_panel = lv_obj_get_child(footer, 1);
    lv_obj_t* reply_label = lv_obj_get_child(reply_panel, 0);
    lv_obj_t* content = lv_obj_get_child(frame, 1);
    assert(lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_height(footer) == 72 && lv_obj_get_width(avatar) == 40);
    assert(lv_obj_get_x(avatar) == 30);

    send_request("1", 5, true, "Done", 0);
    assert(last_error == Dp_ErrNone && strcmp(last_revision, "1") == 0);
    assert(spark_display_current()->count == 5);
    assert(strcmp(spark_display_current()->rows[0].secondary, "Body content") == 0);
    lv_refr_now(display);
    save_frame(argc > 1 ? argv[1] : NULL, "list");
    lv_obj_t* last_row = lv_obj_get_child(content, 7);
    assert(lv_obj_get_y(last_row) + lv_obj_get_height(last_row) <= lv_obj_get_content_height(content));
    assert(lv_obj_get_child_count(content) == 8);
    assert(lv_obj_get_width(lv_obj_get_child(last_row, 1)) > 200);
    assert(spark_reply_set("Okay."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "reply-short");
    lv_coord_t short_width = lv_obj_get_width(reply_panel);
    assert(lv_obj_get_height(reply_panel) == 56);
    assert(lv_obj_get_style_align(reply_label, LV_PART_MAIN) == LV_ALIGN_LEFT_MID);
    assert(lv_obj_get_style_text_align(reply_label, LV_PART_MAIN) == LV_TEXT_ALIGN_LEFT);
    assert(spark_reply_set("I found three notes from yesterday."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "reply-medium");
    lv_coord_t medium_width = lv_obj_get_width(reply_panel);
    assert(medium_width > short_width && lv_obj_get_height(reply_panel) == 56);
    assert(spark_reply_set("This is a longer assistant response that exceeds the fixed reply area and must end with an ellipsis instead of making the box taller, even when several more words arrive after the visible limit."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "reply-long");
    assert(lv_obj_get_width(reply_panel) >= medium_width);
    assert(lv_obj_get_height(reply_panel) == 56);
    assert(lv_obj_get_height(reply_label) <=
           lv_font_get_line_height(lv_obj_get_style_text_font(reply_label, LV_PART_MAIN)) * 2 + 2);
    assert(lv_obj_get_x(reply_panel) + lv_obj_get_width(reply_panel) <=
           lv_obj_get_content_width(footer) - 24);
    assert(spark_reply_set("Done"));
    lv_refr_now(display);
    const spark_display_t* previous = spark_display_current();
    unsigned before = frames;
    flushed_pixels = 0;
    send_request("1", 5, true, "Done", 0);
    lv_refr_now(display);
    assert(frames == before && flushed_pixels == 0);
    assert(spark_display_current() == previous);

    send_request("2", -1, true, "New reply", 0);
    lv_refr_now(display);
    assert(spark_display_current() == previous && frames == before + 1 && flushed_pixels > 0);
    assert(strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(strcmp(last_command, "selected") == 0 && strcmp(last_revision, "2") == 0);
    assert(spark_display_current()->selected == 1);
    for (unsigned invalid = 1; invalid <= 9; ++invalid) {
        if (invalid == 8) continue;
        send_request("3", 2, true, "Must not apply", invalid);
        assert(last_error == ErrBadParam && spark_display_current() == previous);
        assert(strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    }
    test_layout = SPARK_LAYOUT_NOTE;
    send_request("3", 21, true, NULL, 0); assert(last_error == ErrBadParam);
    test_layout = SPARK_LAYOUT_EMAIL;
    send_request("3", 2, true, NULL, 10); assert(last_error == ErrBadParam);
    test_layout = SPARK_LAYOUT_REMINDER;
    send_request("3", 21, true, NULL, 0); assert(last_error == ErrBadParam);
    send_request("3", 0, false, NULL, 0); assert(last_error == ErrBadParam);
    send_request("3", -1, true, NULL, 0); assert(last_error == ErrBadParam);
    send_request("1", -1, true, "stale", 0); assert(last_error == ErrSeqErr);
    const char* invalid_revisions[] = {"", "01", "-1", "18446744073709551616"};
    for (size_t i = 0; i < 4; ++i) {
        send_request(invalid_revisions[i], -1, true, "bad", 0); assert(last_error == ErrBadParam);
    }
    send_request("3", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    lv_refr_now(display);
    save_frame(argc > 1 ? argv[1] : NULL, "item");
    lv_obj_t* lead = lv_obj_get_child(content, 0);
    lv_obj_t* body = lv_obj_get_child(content, 1);
    assert(lv_obj_get_y(body) > lv_obj_get_y(lead) + lv_obj_get_height(lead));
    uint16_t* partial = malloc(sizeof(screen_pixels)); assert(partial);
    memcpy(partial, screen_pixels, sizeof(screen_pixels));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    assert(memcmp(partial, screen_pixels, sizeof(screen_pixels)) == 0); free(partial);
    test_mixed = true;
    send_request("4", 3, true, NULL, 0);
    assert(last_error == Dp_ErrNone);
    lv_refr_now(display);
    save_frame(argc > 1 ? argv[1] : NULL, "mixed");
    lv_obj_t* email_row = lv_obj_get_child(content, 4);
    lv_obj_t* name = lv_obj_get_child(email_row, 1);
    lv_obj_t* address = lv_obj_get_child(email_row, 4);
    lv_obj_t* time = lv_obj_get_child(email_row, 3);
    assert(lv_obj_get_x(name) + lv_obj_get_width(name) <= lv_obj_get_x(address));
    assert(lv_obj_get_x(address) + lv_obj_get_width(address) <= lv_obj_get_x(time));
    lv_obj_t* event_row = lv_obj_get_child(content, 5);
    assert(lv_obj_get_y(event_row) + lv_obj_get_height(event_row) <= SPARK_DISPLAY_CONTENT_HEIGHT);
    assert(strcmp(spark_display_current()->rows[1].secondary,
                  "Scan results - Sending the MRI from Tuesday. Please review before Thursday.") == 0);
    previous = spark_display_current();
    const char* label_buffer = lv_label_get_text(name);
    test_selected = 1;
    flushed_pixels = 0;
    send_request("5", 3, true, NULL, 0);
    lv_refr_now(display);
    assert(spark_display_current() == previous && spark_display_current()->selected == 1);
    assert(lv_label_get_text(name) == label_buffer); // No text allocation/layout on selection.
    assert(flushed_pixels > 0 && flushed_pixels < 540 * 256);
    partial = malloc(sizeof(screen_pixels)); assert(partial);
    memcpy(partial, screen_pixels, sizeof(screen_pixels));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    assert(memcmp(partial, screen_pixels, sizeof(screen_pixels)) == 0); free(partial);
    test_selected = 0;
    test_mixed = false;
    test_layout = SPARK_LAYOUT_NOTE;
    send_request("6", 3, true, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "notes");
    test_layout = SPARK_LAYOUT_EMAIL;
    send_request("7", 3, true, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "email");
    test_layout = SPARK_LAYOUT_EVENT;
    send_request("8", 2, true, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_refr_now(display); save_frame(argc > 1 ? argv[1] : NULL, "calendar");
    send_request("9", 0, true, "", 0);
    assert(lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));

    test_layout = SPARK_LAYOUT_REMINDER;
    test_display_id = "result-a";
    send_request("10", 20, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->count == 20);
    // No phone response is supplied during these swipes, including page crossings.
    for (unsigned i = 0; i < 7; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 7 && spark_display_current()->page_index == 2);
    assert(strcmp(last_artifact_id, "item-7") == 0 && last_navigation_sequence == 7);
    assert(strcmp(last_display_id, "result-a") == 0);
    lv_refr_now(display);
    assert(lv_obj_get_child_count(content) == 8);
    for (unsigned i = 0; i < 30; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 19 && spark_display_current()->page_index == 4);
    for (unsigned i = 0; i < 30; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_RIGHT, NULL);
    assert(spark_display_current()->selected == 0 && spark_display_current()->page_index == 1);

    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(!spark_display_current()->is_list);
    assert(strcmp(last_command, "open") == 0 && strcmp(last_artifact_id, "item-0") == 0);
    test_navigation_sequence = last_navigation_sequence;
    send_request("11", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && !spark_display_current()->is_list);
    lv_obj_send_event(parent, LV_EVENT_DCLICKED, NULL);
    assert(spark_display_current()->is_list && spark_display_current()->count == 20);
    assert(strcmp(last_command, "selected") == 0 && strcmp(last_artifact_id, "item-0") == 0);
    // A delayed detail response must not reverse a local Back.
    send_request("12", 1, false, NULL, 0);
    assert(spark_display_current()->is_list && strcmp(last_command, "selected") == 0);
    uint64_t correction = last_navigation_sequence;
    send_request("12", 1, false, NULL, 0);
    assert(spark_display_current()->is_list && last_navigation_sequence > correction);
    test_navigation_sequence = last_navigation_sequence;
    previous = spark_display_current();
    send_request("13", 21, true, NULL, 0); assert(last_error == ErrBadParam);
    send_request("13", 20, true, NULL, 12); assert(last_error == ErrBadParam);
    char oversized[SPARK_DISPLAY_MAX_REQUEST_BYTES + 1];
    memset(oversized, 'a', sizeof(oversized) - 1); oversized[sizeof(oversized) - 1] = '\0';
    send_request("13", -1, true, oversized, 0); assert(last_error == ErrBadParam);
    assert(spark_display_current() == previous);
    drop_report = true;
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 1);
    drop_report = false;
    send_request("13", 1, false, NULL, 0);
    assert(spark_display_current()->is_list && spark_display_current()->selected == 1);
    // Explicit detail replacement must release the cached list.
    test_display_id = "standalone";
    send_request("14", 1, false, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_obj_send_event(parent, LV_EVENT_DCLICKED, NULL);
    assert(!spark_display_current()->is_list);
    // Receiver-derived pages use the same fixed-height packing as the phone mirror.
    const char* ids[] = {"notes", "emails", "events"};
    for (unsigned i = 0; i < 3; ++i) {
        test_display_id = ids[i];
        test_layout = SPARK_LAYOUT_NOTE + i;
        test_selected = 19;
        char revision[21]; snprintf(revision, sizeof(revision), "%u", 15 + i);
        send_request(revision, 20, true, NULL, 0);
        const spark_display_t* packed = spark_display_current();
        assert(last_error == Dp_ErrNone && packed->selected == 19);
        assert(packed->page_count == (i == 2 ? 10 : 7));
        assert(packed->page_index == packed->page_count);
        assert(packed->page_starts[1] == (i == 2 ? 2 : 3));
    }
    test_selected = 0;
    app->on_pause(); assert(spark_display_current() == NULL);
    assert(spark_assistant_current()->state == SPARK_ASSISTANT_IDLE);
    send_request("1", 1, false, "Reconnected", 0); assert(last_error == Dp_ErrNone);
    const char* assistant_states[] = {"idle", "listening", "thinking", "working", "error"};
    const spark_assistant_state_t expected_states[] = {
        SPARK_ASSISTANT_IDLE, SPARK_ASSISTANT_LISTENING, SPARK_ASSISTANT_THINKING,
        SPARK_ASSISTANT_WORKING, SPARK_ASSISTANT_ERROR};
    for (size_t i = 0; i < 5; ++i) {
        char revision[4]; snprintf(revision, sizeof(revision), "%zu", i + 2);
        const void* previous_avatar_frame = lv_image_get_src(avatar_image);
        test_assistant_state = assistant_states[i];
        test_assistant_detail = i == 3 ? "search_notes" : NULL;
        send_request(revision, -1, true, NULL, 0);
        assert(last_error == Dp_ErrNone && spark_assistant_current()->state == expected_states[i]);
        assert(strcmp(spark_assistant_current()->detail,
                      test_assistant_detail == NULL ? "" : test_assistant_detail) == 0);
        if (expected_states[i] == SPARK_ASSISTANT_LISTENING) {
            lv_tick_inc(540); lv_anim_refr_now();
            assert(lv_image_get_src(avatar_image) != previous_avatar_frame);
            lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
            save_frame(argc > 1 ? argv[1] : NULL, "assistant-listening");
        } else if (expected_states[i] == SPARK_ASSISTANT_THINKING) {
            const void* morph_start = lv_image_get_src(avatar_image);
            lv_tick_inc(180); lv_anim_refr_now();
            assert(lv_image_get_src(avatar_image) != morph_start);
            lv_tick_inc(180); lv_anim_refr_now();
            lv_tick_inc(300); lv_anim_refr_now();
            lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
            save_frame(argc > 1 ? argv[1] : NULL, "assistant-thinking");
        } else if (expected_states[i] == SPARK_ASSISTANT_WORKING) {
            const void* pen = lv_image_get_src(avatar_image);
            assert(pen != previous_avatar_frame);
            lv_tick_inc(500); lv_anim_refr_now();
            assert(lv_image_get_src(avatar_image) == pen);
            lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
            save_frame(argc > 1 ? argv[1] : NULL, "assistant-working");
        }
    }
    test_assistant_state = "unknown";
    test_assistant_detail = NULL;
    send_request("7", -1, true, NULL, 0); assert(last_error == ErrBadParam);
    assert(spark_assistant_current()->state == SPARK_ASSISTANT_ERROR);
    char oversized_detail[SPARK_ASSISTANT_MAX_DETAIL + 2];
    memset(oversized_detail, 'a', sizeof(oversized_detail) - 1);
    oversized_detail[sizeof(oversized_detail) - 1] = '\0';
    test_assistant_state = "working";
    test_assistant_detail = oversized_detail;
    send_request("7", -1, true, NULL, 0); assert(last_error == ErrBadParam);
    assert(spark_assistant_current()->state == SPARK_ASSISTANT_ERROR);
    test_assistant_state = NULL;
    test_assistant_detail = NULL;
    active_app = "prompter";
    send_request("7", 1, false, NULL, 0); assert(last_error == ErrNotReady);
    app->on_stop(); spark_page_get()->on_destroy(); lv_obj_delete(parent);
    lv_display_delete(display); lv_deinit();
    puts("Spark display: partial redraw, five-row fit, independent updates, retry ACK, validation, ownership, layout and reports passed.");
}
