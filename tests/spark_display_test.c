#include "apps/spark/spark.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"
#include "system/system_res.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static app_t* app;
static app_message_t* receiver;
static lv_obj_t* parent;
static unsigned frames, flushed_pixels, flushed_at_ack;
static MsgDpErr last_error;
static char last_revision[21], last_command[32];
static uint8_t outgoing_type;
static const char* active_app = "home";
static uint16_t screen_pixels[540 * 440];
static unsigned test_selected;
static spark_item_type_t test_type = SPARK_ITEM_TODO;
// When set, list items take their types from here in turn.
static const spark_item_type_t* test_types;
static size_t test_type_count;
static bool test_completed;
static const char* test_display_id = "";
static uint64_t test_navigation_sequence, last_navigation_sequence;
static char last_artifact_id[257], last_display_id[65];
static bool drop_report;
static const char* test_assistant_state;
static const char* test_assistant_detail;
// A single item is a card: a grid with count rows, or text with count blocks.
static bool test_grid;
static size_t test_grid_columns = 2;
static bool test_doc;
static unsigned reports;
static uint64_t test_now_ms;
static const char* test_screen;
static bool test_screen_bad_type;
static uint8_t test_sys_state = 1;
static unsigned sys_state_sets;
static char last_sys_state_trigger[32];

void system_set_sys_state(uint8_t state) { test_sys_state = state; ++sys_state_sets; }
bool system_report_sys_state(uint8_t state, const char* trigger) {
    assert(state == test_sys_state);
    snprintf(last_sys_state_trigger, sizeof(last_sys_state_trigger), "%s", trigger);
    return true;
}

uint64_t spark_monotonic_ms(void) { return test_now_ms; }
static system_sys_state_listener_t sys_state_listener;
void system_set_sys_state_listener(system_sys_state_listener_t listener) {
    sys_state_listener = listener;
}
uint32_t system_runtime_state_get_btconn_event(void) {
    static uint32_t id;
    if (id == 0) id = lv_event_register_id();
    return id;
}
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
bool system_ui_paint_now(void) {
    lv_refr_now(lv_display_get_default());
    return true;
}
void system_status_bar_set_mode_at(bool visible, status_bar_widget_pos_t pos) {
    (void)visible;
    (void)pos;
}
const lv_font_t* get_font_by_size_near(uint32_t size) { return size >= 24 ? &lv_font_montserrat_24 : &lv_font_montserrat_18; }
bool app_mpack_send_ack(msg_pack_t* msg, MsgDpErr error) {
    assert(msg->id == APP_MSG_ID_HOME);
    flushed_at_ack = flushed_pixels;
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
    if (outgoing_type == MSG_TYPE_ACK) flushed_at_ack = flushed_pixels;
    assert(mpack_writer_destroy(&writer->writer) == mpack_ok);
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, writer->buffer, writer->size);
    mpack_tree_parse(&tree);
    mpack_node_t revision = mpack_node_map_cstr(mpack_tree_root(&tree),
        outgoing_type == MSG_TYPE_ACK ? "batch_id" : "revision");
    mpack_node_copy_utf8_cstr(revision, last_revision, sizeof(last_revision));
    if (outgoing_type != MSG_TYPE_ACK) {
        ++reports;
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

#define DETAIL_LEAD "A long lead that wraps across several lines on this display. A long lead that wraps across several lines on this display. A long lead that wraps across several lines on this display."

static const char* const type_names[SPARK_ITEM_TYPE_COUNT] = {
    "todo", "note", "email", "email_draft", "calendar_event", "contact", "places", "route", "card",
};

// A type's text fields and their values, required ones first.
static size_t item_fields(spark_item_type_t type, bool detail, const char** keys, const char** values) {
    size_t n = 0;
#define FIELD(key, value) (keys[n] = (key), values[n++] = (value))
    switch (type) {
    case SPARK_ITEM_TODO:
        FIELD("content", detail ? DETAIL_LEAD : "Call Kim back");
        FIELD("due", "Tomorrow"); FIELD("repeat", "Weekly");
        break;
    case SPARK_ITEM_NOTE:
        FIELD("title", "Meeting notes"); FIELD("content", "Body content\nSecond line");
        FIELD("date", "Sep 5, 2026");
        break;
    case SPARK_ITEM_EMAIL:
        FIELD("sender", "Dr Okafor"); FIELD("subject", "Scan results");
        FIELD("preview", "Sending the MRI from Tuesday. Please review before Thursday.");
        FIELD("time", "10:43"); FIELD("sentAt", "Sep 5, 2026 at 10:43 AM");
        FIELD("address", "dr.okafor@example.com");
        break;
    case SPARK_ITEM_EMAIL_DRAFT:
        FIELD("to", "kim@example.com"); FIELD("from", "me@example.com");
        FIELD("subject", "Project update"); FIELD("body", "Here is the project update.");
        FIELD("status", "Composing");
        break;
    case SPARK_ITEM_CALENDAR_EVENT:
        FIELD("title", "Studio crit"); FIELD("when", "Sep 5, 2026, 10:00 AM - 11:00 AM");
        FIELD("location", "Room 2"); FIELD("response", "Accepted");
        break;
    case SPARK_ITEM_CONTACT:
        FIELD("name", "Sam Lee"); FIELD("organization", "Acme"); FIELD("jobTitle", "Designer");
        FIELD("phone", "+1 555 0100"); FIELD("email", "sam@example.com");
        break;
    case SPARK_ITEM_PLACES:
        FIELD("name", "Blue Bottle Coffee"); FIELD("address", "66 Mint St");
        FIELD("rating", "4.5 (1,200)"); FIELD("open", "Open now");
        break;
    case SPARK_ITEM_ROUTE:
        FIELD("title", "Home to Work"); FIELD("via", "via I-280"); FIELD("duration", "24 mins");
        FIELD("distance", "18 km"); FIELD("mode", "Drive");
        break;
    case SPARK_ITEM_CARD:
    case SPARK_ITEM_TYPE_COUNT:
        break;
    }
#undef FIELD
    return n;
}

// invalid: 1 duplicate id, 2 NUL in text, 4 bad UTF-8, 5 text over 4096 bytes, 6 unknown type,
// 7 a required field missing, 8 a flag that is not a bool, 9 a key the type does not have.
static void write_item(mpack_writer_t* writer, spark_item_type_t type, int index, bool detail,
                       unsigned invalid) {
    char id[32]; snprintf(id, sizeof(id), "item-%d", invalid == 1 ? 0 : index);
    if (type == SPARK_ITEM_CARD) {
        mpack_start_map(writer, 4);
        text(writer, "id", id); text(writer, "type", "card");
        mpack_write_cstr(writer, "hasMore"); mpack_write_bool(writer, false);
        mpack_write_cstr(writer, "blocks"); mpack_start_array(writer, 2);
        mpack_start_array(writer, 2); text(writer, "h", "Pack"); mpack_finish_array(writer);
        mpack_start_array(writer, 2); text(writer, "p", "• Charger\n• Passport"); mpack_finish_array(writer);
        mpack_finish_array(writer);
        mpack_finish_map(writer);
        return;
    }
    const char* keys[SPARK_ITEM_MAX_FIELDS]; const char* values[SPARK_ITEM_MAX_FIELDS];
    size_t count = item_fields(type, detail, keys, values);
    bool todo = type == SPARK_ITEM_TODO;
    size_t skip = invalid == 7 ? 1 : 0;
    mpack_start_map(writer, 3 + (uint32_t)(count - skip) + todo + (invalid == 9));
    text(writer, "id", id);
    text(writer, "type", invalid == 6 ? "bad" : type_names[type]);
    mpack_write_cstr(writer, "hasMore"); mpack_write_bool(writer, false);
    for (size_t f = skip; f < count; ++f) {
        mpack_write_cstr(writer, keys[f]);
        if (f == 0 && invalid == 2) mpack_write_str(writer, "bad\0text", 8);
        else if (f == 0 && invalid == 4) mpack_write_str(writer, "\xff", 1);
        else if (f == 0 && invalid == 5) {
            char huge[SPARK_DISPLAY_MAX_TEXT + 1]; memset(huge, 'a', sizeof(huge));
            mpack_write_str(writer, huge, sizeof(huge));
        } else mpack_write_cstr(writer, values[f]);
    }
    if (todo) {
        mpack_write_cstr(writer, "completed");
        if (invalid == 8) mpack_write_cstr(writer, "no");
        else mpack_write_bool(writer, test_completed);
    }
    if (invalid == 9) text(writer, "mark", "[ ]");
    mpack_finish_map(writer);
}

// A text card with count blocks. invalid 1: bad tag; 2: empty text; 3: a bare string block;
// 4: headers and rows as well.
static void write_doc(mpack_writer_t* writer, int count, unsigned invalid) {
    mpack_start_map(writer, invalid == 4 ? 6 : 4);
    text(writer, "id", "card-0"); text(writer, "type", "card");
    mpack_write_cstr(writer, "hasMore"); mpack_write_bool(writer, false);
    mpack_write_cstr(writer, "blocks");
    mpack_start_array(writer, count);
    for (int i = 0; i < count; ++i) {
        if (invalid == 3 && i == 0) { mpack_write_cstr(writer, "loose"); continue; }
        mpack_start_array(writer, 2);
        mpack_write_cstr(writer, invalid == 1 && i == 0 ? "x" : i % 2 == 0 ? "h" : "p");
        if (invalid == 2 && i == 1) mpack_write_cstr(writer, "");
        else if (i % 2 == 0) mpack_write_cstr(writer, i == 0 ? "Reset router" : "If it still fails");
        else mpack_write_cstr(writer, "Unplug it and wait 30 seconds.\n• Plug the modem in first\n• Then the router, and give it a minute to settle");
        mpack_finish_array(writer);
    }
    mpack_finish_array(writer);
    if (invalid == 4) {
        mpack_write_cstr(writer, "headers"); mpack_start_array(writer, 2);
        mpack_write_cstr(writer, "A"); mpack_write_cstr(writer, "B"); mpack_finish_array(writer);
        mpack_write_cstr(writer, "rows"); mpack_start_array(writer, 1); mpack_start_array(writer, 2);
        mpack_write_cstr(writer, "1"); mpack_write_cstr(writer, "2");
        mpack_finish_array(writer); mpack_finish_array(writer);
    }
    mpack_finish_map(writer);
}

// A grid card with count rows. invalid 1: a ragged row; 2: four columns; 3: a number cell.
static void write_grid(mpack_writer_t* writer, int count, unsigned invalid) {
    size_t columns = invalid == 2 ? 4 : test_grid_columns;
    const char* headers[] = {"Train", "Drive", "Walk", "Fly"};
    mpack_start_map(writer, 5);
    text(writer, "id", "card-0"); text(writer, "type", "card");
    mpack_write_cstr(writer, "hasMore"); mpack_write_bool(writer, false);
    mpack_write_cstr(writer, "headers");
    mpack_start_array(writer, columns);
    for (size_t c = 0; c < columns; ++c) mpack_write_cstr(writer, headers[c]);
    mpack_finish_array(writer);
    mpack_write_cstr(writer, "rows");
    mpack_start_array(writer, count);
    for (int r = 0; r < count; ++r) {
        size_t width = invalid == 1 && r == 1 ? columns - 1 : columns;
        mpack_start_array(writer, width);
        for (size_t c = 0; c < width; ++c) {
            if (invalid == 3 && r == 0 && c == 0) { mpack_write_u32(writer, 7); continue; }
            char cell[64];
            if (r == 1 && c == 1) snprintf(cell, sizeof(cell), "Door to door, then a short walk");
            else snprintf(cell, sizeof(cell), "Cell %d-%zu", r, c);
            mpack_write_cstr(writer, cell);
        }
        mpack_finish_array(writer);
    }
    mpack_finish_array(writer);
    mpack_finish_map(writer);
}

// count < 0 omits the page. A NULL reply omits the footer update. A list sends
// `page` with count items; otherwise one `item` is sent (a card's count is its
// rows or blocks). invalid 3: selection out of range; 12: a title over 256 bytes.
static void send_request(const char* revision, int count, bool list, const char* reply, unsigned invalid) {
    char* bytes = NULL; size_t size = 0;
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, &bytes, &size);
    mpack_start_map(&writer, 3 + (count >= 0) + (reply != NULL) +
                                 (test_assistant_state != NULL) +
                                 (test_screen != NULL || test_screen_bad_type));
    text(&writer, "revision", revision);
    if (test_screen_bad_type) { mpack_write_cstr(&writer, "screen"); mpack_write_u8(&writer, 0); }
    else if (test_screen != NULL) text(&writer, "screen", test_screen);
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
    if (count >= 0 && list) {
        mpack_write_cstr(&writer, "page"); mpack_start_map(&writer, 4);
        if (invalid == 12) {
            char long_title[SPARK_DISPLAY_MAX_LIST_TEXT + 1]; memset(long_title, 'a', sizeof(long_title));
            mpack_write_cstr(&writer, "title"); mpack_write_str(&writer, long_title, sizeof(long_title));
        } else text(&writer, "title", "Items");
        text(&writer, "hint", "8 items");
        mpack_write_cstr(&writer, "selected"); mpack_write_u32(&writer, invalid == 3 ? 10 : test_selected);
        mpack_write_cstr(&writer, "items"); mpack_start_array(&writer, count);
        for (int i = 0; i < count; ++i) {
            spark_item_type_t type = test_types != NULL ? test_types[i % test_type_count] : test_type;
            write_item(&writer, type, i, false, invalid == 12 ? 0 : invalid);
        }
        mpack_finish_array(&writer); mpack_finish_map(&writer);
    } else if (count >= 0) {
        mpack_write_cstr(&writer, "item");
        if (test_grid) write_grid(&writer, count, invalid);
        else if (test_doc) write_doc(&writer, count, invalid);
        else { assert(count == 1); write_item(&writer, test_type, 0, true, invalid); }
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

static const char* current_text(size_t index, const char* key) {
    return spark_item_text(&spark_display_current()->items[index], key);
}

int main(int argc, char** argv) {
    const char* artifacts = argc > 1 ? argv[1] : NULL;
    lv_init();
    lv_display_t* display = lv_display_create(540, 440);
    static uint16_t pixels[540 * 40];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    parent = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(parent); lv_obj_set_size(parent, 540, 415); lv_obj_set_pos(parent, 0, 25);
    assert(spark_app_register()); app->on_start();
    lv_obj_t* footer = lv_obj_get_child(parent, 1);
    lv_obj_t* avatar = lv_obj_get_child(footer, 0);
    lv_obj_t* avatar_image = lv_obj_get_child(avatar, 0);
    // The avatar is a still image; it starts on its idle frame.
    assert(lv_image_get_src(avatar_image) != NULL);
    lv_obj_t* frame = lv_obj_get_child(parent, 0);
    lv_obj_t* header = lv_obj_get_child(frame, 0);
    lv_obj_t* title_label = lv_obj_get_child(header, 0);
    lv_obj_t* reply_panel = lv_obj_get_child(footer, 1);
    lv_obj_t* reply_label = lv_obj_get_child(reply_panel, 0);
    lv_obj_t* content = lv_obj_get_child(frame, 1);
    assert(lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));
    lv_obj_update_layout(parent);
    assert(lv_obj_get_height(footer) == 84 && lv_obj_get_width(avatar) == 40);
    assert(lv_obj_get_x(avatar) == 30);
    assert(lv_obj_get_y(avatar) + lv_obj_get_height(avatar) / 2 ==
           lv_obj_get_y(reply_panel) + lv_obj_get_height(reply_panel) / 2);

    send_request("1", 5, true, "Done", 0);
    assert(last_error == Dp_ErrNone && strcmp(last_revision, "1") == 0);
    assert(spark_display_current()->count == 5);
    assert(spark_display_current()->items[0].type == SPARK_ITEM_TODO);
    assert(strcmp(current_text(0, "content"), "Call Kim back") == 0);
    lv_refr_now(display);
    save_frame(artifacts, "list");
    lv_obj_t* last_row = lv_obj_get_child(content, 7);
    assert(lv_obj_get_y(last_row) + lv_obj_get_height(last_row) <= lv_obj_get_content_height(content));
    assert(lv_obj_get_child_count(content) == 10);
    assert(lv_obj_get_width(lv_obj_get_child(last_row, 1)) > 200);
    assert(spark_reply_set("Okay."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(artifacts, "reply-short");
    lv_coord_t short_width = lv_obj_get_width(reply_panel);
    assert(lv_obj_get_height(reply_panel) == 68);
    assert(lv_obj_get_style_align(reply_label, LV_PART_MAIN) == LV_ALIGN_LEFT_MID);
    assert(lv_obj_get_style_text_align(reply_label, LV_PART_MAIN) == LV_TEXT_ALIGN_LEFT);
    assert(spark_reply_set("I found three notes from yesterday."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(artifacts, "reply-medium");
    lv_coord_t medium_width = lv_obj_get_width(reply_panel);
    assert(medium_width > short_width && lv_obj_get_height(reply_panel) == 68);
    assert(spark_reply_set("This is a longer assistant response that exceeds the fixed reply area and must end with an ellipsis instead of making the box taller, even when several more words arrive after the visible limit."));
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display); save_frame(artifacts, "reply-long");
    assert(lv_obj_get_width(reply_panel) >= medium_width);
    assert(lv_obj_get_height(reply_panel) == 68);
    assert(lv_obj_get_height(reply_label) <=
           lv_font_get_line_height(lv_obj_get_style_text_font(reply_label, LV_PART_MAIN)) * 3 - 4);
    assert(lv_obj_get_content_height(reply_panel) >=
           lv_font_get_line_height(lv_obj_get_style_text_font(reply_label, LV_PART_MAIN)) * 3 - 4);
    assert(lv_obj_get_x(reply_panel) + lv_obj_get_width(reply_panel) <=
           lv_obj_get_content_width(footer) - 8);
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
    // The handler paints before it ACKs and never asks the system for a full refresh.
    assert(spark_display_current() == previous && frames == before && flushed_at_ack > 0);
    lv_refr_now(display);
    assert(flushed_pixels == flushed_at_ack);
    assert(strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    flushed_pixels = 0;
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(flushed_pixels > 0 && flushed_pixels < 540 * 256 && frames == before); // Swipes paint now.
    assert(strcmp(last_command, "selected") == 0 && strcmp(last_revision, "2") == 0);
    assert(spark_display_current()->selected == 1);
    for (unsigned invalid = 1; invalid <= 9; ++invalid) {
        send_request("3", 2, true, "Must not apply", invalid);
        assert(last_error == ErrBadParam && spark_display_current() == previous);
        assert(strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    }
    test_type = SPARK_ITEM_NOTE;
    send_request("3", 21, true, NULL, 0); assert(last_error == ErrBadParam);
    test_type = SPARK_ITEM_TODO;
    send_request("3", 21, true, NULL, 0); assert(last_error == ErrBadParam);
    send_request("3", 1, false, NULL, 6); assert(last_error == ErrBadParam);
    send_request("3", -1, true, NULL, 0); assert(last_error == ErrBadParam);
    send_request("1", -1, true, "stale", 0); assert(last_error == ErrSeqErr);
    const char* invalid_revisions[] = {"", "01", "-1", "18446744073709551616"};
    for (size_t i = 0; i < 4; ++i) {
        send_request(invalid_revisions[i], -1, true, "bad", 0); assert(last_error == ErrBadParam);
    }
    send_request("3", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && strcmp(lv_label_get_text(reply_label), "New reply") == 0);
    lv_refr_now(display);
    save_frame(artifacts, "item");
    lv_obj_t* lead = lv_obj_get_child(content, 0);
    lv_obj_t* body = lv_obj_get_child(content, 1);
    assert(strcmp(lv_label_get_text(title_label), "Reminder") == 0);
    assert(strcmp(lv_label_get_text(lead), DETAIL_LEAD) == 0);
    assert(strcmp(lv_label_get_text(body), "Tomorrow · Weekly") == 0);
    assert(lv_obj_get_style_text_font(body, LV_PART_MAIN) ==
           lv_obj_get_style_text_font(reply_label, LV_PART_MAIN));
    assert(lv_obj_get_y(body) > lv_obj_get_y(lead) + lv_obj_get_height(lead));
    uint16_t* partial = malloc(sizeof(screen_pixels)); assert(partial);
    memcpy(partial, screen_pixels, sizeof(screen_pixels));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    assert(memcmp(partial, screen_pixels, sizeof(screen_pixels)) == 0); free(partial);

    static const spark_item_type_t mixed[] = {
        SPARK_ITEM_NOTE, SPARK_ITEM_EMAIL, SPARK_ITEM_CALENDAR_EVENT,
    };
    test_types = mixed; test_type_count = 3;
    send_request("4", 3, true, NULL, 0);
    assert(last_error == Dp_ErrNone);
    lv_refr_now(display);
    save_frame(artifacts, "mixed");
    lv_obj_t* note_row = lv_obj_get_child(content, 3);
    // A card is one line: line breaks in its text read as spaces.
    assert(strcmp(lv_label_get_text(lv_obj_get_child(note_row, 2)), "Body content Second line") == 0);
    lv_obj_t* email_row = lv_obj_get_child(content, 4);
    lv_obj_t* name = lv_obj_get_child(email_row, 1);
    lv_obj_t* address = lv_obj_get_child(email_row, 4);
    lv_obj_t* time = lv_obj_get_child(email_row, 3);
    lv_obj_t* subject = lv_obj_get_child(email_row, 5);
    lv_obj_t* preview = lv_obj_get_child(email_row, 2);
    assert(lv_obj_get_x(name) + lv_obj_get_width(name) <= lv_obj_get_x(address));
    assert(lv_obj_get_x(address) + lv_obj_get_width(address) <= lv_obj_get_x(time));
    assert(strcmp(lv_label_get_text(subject), "Scan results") == 0);
    assert(lv_obj_get_x(subject) + lv_obj_get_width(subject) <= lv_obj_get_x(preview));
    lv_obj_t* event_row = lv_obj_get_child(content, 5);
    assert(lv_obj_get_y(event_row) + lv_obj_get_height(event_row) <= SPARK_DISPLAY_CONTENT_HEIGHT);
    assert(strcmp(current_text(1, "preview"),
                  "Sending the MRI from Tuesday. Please review before Thursday.") == 0);
    previous = spark_display_current();
    const char* label_buffer = lv_label_get_text(name);
    test_selected = 1;
    flushed_pixels = 0;
    send_request("5", 3, true, NULL, 0);
    assert(flushed_at_ack > 0 && flushed_at_ack < 540 * 256); // Selection paints two rows, then ACKs.
    lv_refr_now(display);
    assert(spark_display_current() == previous && spark_display_current()->selected == 1);
    assert(lv_label_get_text(name) == label_buffer); // No text allocation/layout on selection.
    assert(flushed_pixels > 0 && flushed_pixels < 540 * 256);
    partial = malloc(sizeof(screen_pixels)); assert(partial);
    memcpy(partial, screen_pixels, sizeof(screen_pixels));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    assert(memcmp(partial, screen_pixels, sizeof(screen_pixels)) == 0); free(partial);
    test_selected = 0;
    test_types = NULL;
    test_type = SPARK_ITEM_NOTE;
    send_request("6", 3, true, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_refr_now(display); save_frame(artifacts, "notes");
    test_type = SPARK_ITEM_EMAIL;
    send_request("7", 4, true, NULL, 0); assert(last_error == Dp_ErrNone);
    assert(spark_display_current()->page_count == 1);
    lv_refr_now(display); save_frame(artifacts, "email");
    test_type = SPARK_ITEM_CALENDAR_EVENT;
    send_request("8", 4, true, NULL, 0); assert(last_error == Dp_ErrNone);
    assert(spark_display_current()->page_count == 1);
    lv_refr_now(display); save_frame(artifacts, "calendar");
    send_request("9", 0, true, "", 0);
    assert(lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));

    test_type = SPARK_ITEM_TODO;
    test_display_id = "result-a";
    send_request("10", 20, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->count == 20);
    // No phone response is supplied during these swipes, including page crossings.
    for (unsigned i = 0; i < 7; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 7 && spark_display_current()->page_index == 2);
    assert(strcmp(last_artifact_id, "item-7") == 0 && last_navigation_sequence == 7);
    assert(strcmp(last_display_id, "result-a") == 0);
    lv_refr_now(display);
    assert(lv_obj_get_child_count(content) == 10);
    for (unsigned i = 0; i < 30; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 19 && spark_display_current()->page_index == 4);
    for (unsigned i = 0; i < 30; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_RIGHT, NULL);
    assert(spark_display_current()->selected == 0 && spark_display_current()->page_index == 1);

    unsigned before_open = reports;
    drop_report = true;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open);
    drop_report = false;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(spark_display_current()->kind == SPARK_DISPLAY_LIST);
    assert(strcmp(last_command, "open") == 0 && strcmp(last_artifact_id, "item-0") == 0);
    assert(reports == before_open + 1);
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open + 1); // The same list state has one open in flight.
    lv_obj_send_event(parent, LV_EVENT_GESTURE_RIGHT, NULL);
    assert(spark_display_current()->selected == 0);
    before_open = reports;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open + 1); // A boundary gesture also cancels the open.
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->selected == 1);
    before_open = reports;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open + 1 && strcmp(last_artifact_id, "item-1") == 0);
    lv_obj_send_event(parent, LV_EVENT_GESTURE_RIGHT, NULL);
    assert(spark_display_current()->selected == 0);
    before_open = reports;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open + 1 && strcmp(last_artifact_id, "item-0") == 0);
    test_navigation_sequence = last_navigation_sequence;
    // An item without its required fields is rejected and does not complete the open.
    send_request("11", 1, false, NULL, 7);
    assert(last_error == ErrBadParam && spark_display_current()->kind == SPARK_DISPLAY_LIST);
    before_open = reports;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open); // A rejected response does not complete the open.
    send_request("11", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->kind == SPARK_DISPLAY_ITEM);
    assert(strcmp(current_text(0, "content"), DETAIL_LEAD) == 0);
    lv_obj_send_event(parent, LV_EVENT_DCLICKED, NULL);
    assert(spark_display_current()->kind == SPARK_DISPLAY_LIST && spark_display_current()->count == 20);
    assert(strcmp(last_command, "selected") == 0 && strcmp(last_artifact_id, "item-0") == 0);
    before_open = reports;
    lv_obj_send_event(parent, LV_EVENT_CLICKED, NULL);
    assert(reports == before_open + 1); // Back changed the navigation state.
    // A delayed detail response must not reverse a local Back.
    send_request("12", 1, false, NULL, 0);
    assert(spark_display_current()->kind == SPARK_DISPLAY_LIST && strcmp(last_command, "selected") == 0);
    uint64_t correction = last_navigation_sequence;
    send_request("12", 1, false, NULL, 0);
    assert(spark_display_current()->kind == SPARK_DISPLAY_LIST && last_navigation_sequence > correction);
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
    assert(spark_display_current()->kind == SPARK_DISPLAY_LIST && spark_display_current()->selected == 1);
    // Explicit detail replacement must release the cached list.
    test_display_id = "standalone";
    send_request("14", 1, false, NULL, 0); assert(last_error == Dp_ErrNone);
    lv_obj_send_event(parent, LV_EVENT_DCLICKED, NULL);
    assert(spark_display_current()->kind == SPARK_DISPLAY_ITEM);
    // A new phone/controller starts a new display and may restart its revision.
    test_display_id = "restarted";
    send_request("1", 2, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->kind == SPARK_DISPLAY_LIST &&
           spark_display_current()->count == 2);
    previous = spark_display_current();
    send_request("0", 1, false, NULL, 0);
    assert(last_error == ErrSeqErr && spark_display_current() == previous);
    test_display_id = "same-revision-new-display";
    send_request("1", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->kind == SPARK_DISPLAY_ITEM);
    // Receiver-derived pages use the same fixed-height packing as the phone mirror.
    const char* ids[] = {"notes", "emails", "events"};
    const spark_item_type_t packed_types[] = {
        SPARK_ITEM_NOTE, SPARK_ITEM_EMAIL, SPARK_ITEM_CALENDAR_EVENT,
    };
    for (unsigned i = 0; i < 3; ++i) {
        test_display_id = ids[i];
        test_type = packed_types[i];
        test_selected = 19;
        char revision[21]; snprintf(revision, sizeof(revision), "%u", 15 + i);
        send_request(revision, 20, true, NULL, 0);
        const spark_display_t* packed = spark_display_current();
        assert(last_error == Dp_ErrNone && packed->selected == 19);
        assert(packed->page_count == (i == 0 ? 4 : 5));
        assert(packed->page_index == packed->page_count);
        assert(packed->page_starts[1] == (i == 0 ? 5 : 4));
    }
    test_selected = 0;
    test_type = SPARK_ITEM_TODO;
    app->on_pause(); assert(spark_display_current() == NULL);
    assert(spark_assistant_current()->state == SPARK_ASSISTANT_IDLE);
    send_request("1", 1, false, "Reconnected", 0); assert(last_error == Dp_ErrNone);
    const char* assistant_states[] = {"idle", "listening", "thinking", "working", "error"};
    const spark_assistant_state_t expected_states[] = {
        SPARK_ASSISTANT_IDLE, SPARK_ASSISTANT_LISTENING, SPARK_ASSISTANT_THINKING,
        SPARK_ASSISTANT_WORKING, SPARK_ASSISTANT_ERROR};
    const void* pen = NULL;
    for (size_t i = 0; i < 5; ++i) {
        char revision[4]; snprintf(revision, sizeof(revision), "%zu", i + 2);
        const void* previous_avatar_frame = lv_image_get_src(avatar_image);
        unsigned refreshes = frames;
        test_assistant_state = assistant_states[i];
        test_assistant_detail = i == 3 ? "search_notes" : NULL;
        send_request(revision, -1, true, NULL, 0);
        assert(last_error == Dp_ErrNone && spark_assistant_current()->state == expected_states[i]);
        assert(frames == refreshes);
        assert(strcmp(spark_assistant_current()->detail,
                      test_assistant_detail == NULL ? "" : test_assistant_detail) == 0);
        if (expected_states[i] == SPARK_ASSISTANT_IDLE || expected_states[i] == SPARK_ASSISTANT_ERROR)
            continue;
        // Each state is its own still image: time passing does not change it.
        const void* still = lv_image_get_src(avatar_image);
        assert(still != previous_avatar_frame);
        lv_tick_inc(660); lv_anim_refr_now();
        assert(lv_image_get_src(avatar_image) == still);
        lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
        if (expected_states[i] == SPARK_ASSISTANT_LISTENING) {
            save_frame(artifacts, "assistant-listening");
        } else if (expected_states[i] == SPARK_ASSISTANT_THINKING) {
            save_frame(artifacts, "assistant-thinking");
            flushed_pixels = 0;
            lv_tick_inc(100); lv_anim_refr_now(); lv_refr_now(display);
            assert(flushed_pixels == 0);
        } else {
            pen = still;
            save_frame(artifacts, "assistant-working");
        }
    }
    test_assistant_state = "notes";
    test_assistant_detail = NULL;
    send_request("7", -1, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_assistant_current()->state == SPARK_ASSISTANT_NOTES);
    assert(pen != NULL && lv_image_get_src(avatar_image) == pen);
    assert(!lv_obj_has_flag(avatar_image, LV_OBJ_FLAG_HIDDEN));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    save_frame(artifacts, "assistant-notes");
    const char* icon_states[] = {"todo", "calendar", "maps", "web"};
    const spark_assistant_state_t expected_icons[] = {
        SPARK_ASSISTANT_TODO, SPARK_ASSISTANT_CALENDAR, SPARK_ASSISTANT_MAPS,
        SPARK_ASSISTANT_WEB};
    const char* icon_frames[] = {
        "assistant-todo", "assistant-calendar", "assistant-maps", "assistant-web"};
    for (size_t i = 0; i < 4; ++i) {
        char revision[4]; snprintf(revision, sizeof(revision), "%zu", i + 8);
        test_assistant_state = icon_states[i];
        send_request(revision, -1, true, NULL, 0);
        assert(last_error == Dp_ErrNone && spark_assistant_current()->state == expected_icons[i]);
        assert(!lv_obj_has_flag(avatar_image, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_child_count(avatar) == 1);
        assert(lv_image_get_src(avatar_image) != pen);
        lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
        save_frame(artifacts, icon_frames[i]);
    }
    test_assistant_state = "unknown";
    test_assistant_detail = NULL;
    send_request("12", -1, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_assistant_current()->state == SPARK_ASSISTANT_WORKING);
    assert(!lv_obj_has_flag(avatar_image, LV_OBJ_FLAG_HIDDEN));
    assert(pen != NULL && lv_image_get_src(avatar_image) == pen);
    char oversized_detail[SPARK_ASSISTANT_MAX_DETAIL + 2];
    memset(oversized_detail, 'a', sizeof(oversized_detail) - 1);
    oversized_detail[sizeof(oversized_detail) - 1] = '\0';
    test_assistant_state = "working";
    test_assistant_detail = oversized_detail;
    send_request("13", -1, true, NULL, 0); assert(last_error == ErrBadParam);
    assert(spark_assistant_current()->state == SPARK_ASSISTANT_WORKING);
    test_assistant_state = NULL;
    test_assistant_detail = NULL;

    // A card has no header band; its text starts at the top.
    test_display_id = "untitled";
    test_doc = true;
    lv_obj_t* doc = lv_obj_get_child(content, 9);
    send_request("1", 2, false, "Read this", 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->kind == SPARK_DISPLAY_ITEM);
    lv_refr_now(display); save_frame(artifacts, "untitled");
    assert(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_height(header) == 0);
    assert(lv_obj_get_y(content) == 6 && lv_obj_get_y(lv_obj_get_child(doc, 0)) == 8);
    assert(lv_obj_get_height(content) > SPARK_DISPLAY_CONTENT_HEIGHT);
    test_doc = false;
    test_display_id = "titled";
    send_request("1", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone); // An item with a title brings the header back.
    assert(!lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_height(header) == 64);
    assert(lv_obj_get_y(content) == 70);

    // A grid lays its columns out on the glasses, without a header.
    test_display_id = "grid";
    test_grid = true;
    lv_obj_t* grid = lv_obj_get_child(content, 8);
    for (unsigned invalid = 1; invalid <= 3; ++invalid) {
        send_request("1", 2, false, NULL, invalid);
        assert(last_error == ErrBadParam && spark_display_current()->items[0].card == NULL);
    }
    send_request("1", 0, false, NULL, 0);
    assert(last_error == ErrBadParam && spark_display_current()->items[0].card == NULL);
    assert(lv_obj_has_flag(grid, LV_OBJ_FLAG_HIDDEN));
    send_request("1", 2, false, "Compare", 0);
    const spark_display_card_t* card = spark_display_current()->items[0].card;
    assert(last_error == Dp_ErrNone && card != NULL && card->is_grid);
    assert(card->grid.column_count == 2 && card->grid.row_count == 2);
    assert(strcmp(card->grid.headers[1], "Drive") == 0);
    assert(strcmp(card->grid.cells[1][1], "Door to door, then a short walk") == 0);
    lv_refr_now(display); save_frame(artifacts, "grid");
    assert(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_y(content) == 6);
    assert(!lv_obj_has_flag(grid, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN));
    lv_obj_t* head_left = lv_obj_get_child(grid, 0);
    lv_obj_t* head_right = lv_obj_get_child(grid, 1);
    lv_obj_t* rule = lv_obj_get_child(grid, 3);
    lv_obj_t* cell_00 = lv_obj_get_child(grid, 4);
    lv_obj_t* cell_01 = lv_obj_get_child(grid, 5);
    lv_obj_t* cell_10 = lv_obj_get_child(grid, 6);
    lv_obj_t* cell_11 = lv_obj_get_child(grid, 7);
    assert(strcmp(lv_label_get_text(head_left), "Train") == 0);
    assert(lv_obj_has_flag(lv_obj_get_child(grid, 2), LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_child(grid, 8) == NULL);
    assert(lv_obj_get_x(head_left) + lv_obj_get_width(head_left) < lv_obj_get_x(head_right));
    assert(lv_obj_get_x(cell_00) + lv_obj_get_width(cell_00) < lv_obj_get_x(cell_01));
    assert(lv_obj_get_x(cell_00) == lv_obj_get_x(head_left) && lv_obj_get_x(cell_01) == lv_obj_get_x(head_right));
    assert(lv_obj_get_width(cell_01) >= 200 && lv_obj_get_x(cell_01) + lv_obj_get_width(cell_01) <= lv_obj_get_content_width(content));
    assert(lv_obj_get_y(rule) > lv_obj_get_y(head_left) + lv_obj_get_height(head_left));
    assert(lv_obj_get_y(cell_00) > lv_obj_get_y(rule));
    assert(lv_obj_get_y(cell_10) == lv_obj_get_y(cell_11) && lv_obj_get_y(cell_10) > lv_obj_get_y(cell_00));
    assert(lv_obj_get_height(cell_11) >= lv_obj_get_height(cell_10)); // The long cell wraps.
    assert(lv_obj_get_height(grid) >= lv_obj_get_y(cell_11) + lv_obj_get_height(cell_11));
    assert(lv_obj_get_style_text_font(cell_00, LV_PART_MAIN) ==
           lv_obj_get_style_text_font(reply_label, LV_PART_MAIN));
    // Three columns and twenty rows overflow the page and scroll like a detail body.
    test_grid_columns = 3;
    send_request("2", 20, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->items[0].card->grid.row_count == 20);
    assert(!lv_obj_has_flag(lv_obj_get_child(grid, 2), LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(lv_obj_get_child(grid, lv_obj_get_child_count(grid) - 1), LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_height(grid) > lv_obj_get_height(content));
    lv_refr_now(display); save_frame(artifacts, "grid-long");
    assert(lv_obj_get_scroll_y(content) == 0);
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(lv_obj_get_scroll_y(content) > 0);
    lv_obj_send_event(parent, LV_EVENT_GESTURE_RIGHT, NULL);
    assert(lv_obj_get_scroll_y(content) == 0);
    test_grid = false;
    test_grid_columns = 2;

    // A text card stacks heading and paragraph labels, without a header.
    test_display_id = "doc";
    test_doc = true;
    for (unsigned invalid = 1; invalid <= 4; ++invalid) {
        send_request("1", 2, false, NULL, invalid);
        assert(last_error == ErrBadParam && spark_display_current()->items[0].card->is_grid);
    }
    send_request("1", 0, false, NULL, 0);
    assert(last_error == ErrBadParam && spark_display_current()->items[0].card->is_grid);
    send_request("1", 25, false, NULL, 0);
    assert(last_error == ErrBadParam && spark_display_current()->items[0].card->is_grid);
    send_request("1", 4, false, "Steps are up", 0);
    card = spark_display_current()->items[0].card;
    assert(last_error == Dp_ErrNone && !card->is_grid);
    assert(card->doc.count == 4 && card->doc.heading[2]);
    assert(strcmp(card->doc.text[2], "If it still fails") == 0);
    lv_refr_now(display); save_frame(artifacts, "doc");
    assert(lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_y(content) == 6);
    assert(!lv_obj_has_flag(doc, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(grid, LV_OBJ_FLAG_HIDDEN));
    lv_obj_t* h0 = lv_obj_get_child(doc, 0);
    lv_obj_t* p0 = lv_obj_get_child(doc, 1);
    lv_obj_t* h1 = lv_obj_get_child(doc, 2);
    lv_obj_t* p1 = lv_obj_get_child(doc, 3);
    assert(lv_obj_get_child(doc, 4) == NULL);
    assert(strcmp(lv_label_get_text(h0), "Reset router") == 0);
    assert(lv_obj_get_y(h0) == 8 && lv_obj_get_x(p0) == 0);
    assert(lv_obj_get_width(p0) == lv_obj_get_content_width(content));
    assert(lv_obj_get_y(p0) >= lv_obj_get_y(h0) + lv_obj_get_height(h0) + 6);
    assert(lv_obj_get_y(h1) >= lv_obj_get_y(p0) + lv_obj_get_height(p0) + 10);
    assert(lv_obj_get_y(p1) >= lv_obj_get_y(h1) + lv_obj_get_height(h1) + 6);
    assert(lv_obj_get_height(p0) > lv_obj_get_height(h0)); // Three wrapped lines.
    assert(lv_obj_get_style_text_font(p0, LV_PART_MAIN) ==
           lv_obj_get_style_text_font(reply_label, LV_PART_MAIN));
    assert(lv_obj_get_height(doc) >= lv_obj_get_y(p1) + lv_obj_get_height(p1));
    send_request("2", 24, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->items[0].card->doc.count == 24);
    assert(lv_obj_get_height(doc) > lv_obj_get_height(content));
    lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(lv_obj_get_scroll_y(content) > 0);
    test_doc = false;

    // Replacing a card with a titled detail restores the header and hides the blocks.
    test_display_id = "titled-again";
    send_request("1", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->items[0].card == NULL);
    assert(!lv_obj_has_flag(header, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_height(header) == 64);
    assert(lv_obj_has_flag(grid, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(doc, LV_OBJ_FLAG_HIDDEN) && lv_label_get_text(h0)[0] == '\0');
    assert(lv_label_get_text(cell_00)[0] == '\0');

    // Every type has its own card in a list, packed by its own height.
    static const spark_item_type_t every_type[] = {
        SPARK_ITEM_TODO, SPARK_ITEM_NOTE, SPARK_ITEM_EMAIL, SPARK_ITEM_EMAIL_DRAFT,
        SPARK_ITEM_CALENDAR_EVENT, SPARK_ITEM_CONTACT, SPARK_ITEM_PLACES, SPARK_ITEM_ROUTE,
        SPARK_ITEM_CARD,
    };
    test_display_id = "every-type";
    test_types = every_type; test_type_count = SPARK_ITEM_TYPE_COUNT;
    test_completed = true;
    send_request("1", SPARK_ITEM_TYPE_COUNT, true, NULL, 0);
    const spark_display_t* all = spark_display_current();
    assert(last_error == Dp_ErrNone && all->count == SPARK_ITEM_TYPE_COUNT);
    for (size_t i = 0; i < SPARK_ITEM_TYPE_COUNT; ++i) assert(all->items[i].type == every_type[i]);
    // 40 + 48 + 60 + 60, then 60 + 48 + 48 + 48, then the card.
    assert(all->page_count == 3 && all->page_starts[1] == 4 && all->page_starts[2] == 8);
    lv_refr_now(display); save_frame(artifacts, "types-1");
    lv_obj_t* todo_row = lv_obj_get_child(content, 3);
    assert(lv_obj_get_style_bg_opa(lv_obj_get_child(todo_row, 0), LV_PART_MAIN) == LV_OPA_COVER);
    lv_obj_t* draft_row = lv_obj_get_child(content, 6);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(draft_row, 1)), "kim@example.com") == 0);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(draft_row, 3)), "Composing") == 0);
    for (unsigned i = 0; i < 4; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->page_index == 2);
    lv_refr_now(display); save_frame(artifacts, "types-2");
    lv_obj_t* contact_row = lv_obj_get_child(content, 4);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(contact_row, 1)), "Sam Lee") == 0);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(contact_row, 2)), "Designer · Acme") == 0);
    lv_obj_t* route_row = lv_obj_get_child(content, 6);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(route_row, 3)), "24 mins") == 0);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(route_row, 2)), "18 km · via I-280") == 0);
    for (unsigned i = 0; i < 4; ++i) lv_obj_send_event(parent, LV_EVENT_GESTURE_LEFT, NULL);
    assert(spark_display_current()->page_index == 3);
    lv_refr_now(display); save_frame(artifacts, "types-3");
    lv_obj_t* card_row = lv_obj_get_child(content, 3);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(card_row, 1)), "Pack") == 0);
    assert(strcmp(lv_label_get_text(lv_obj_get_child(card_row, 2)), "• Charger • Passport") == 0);
    test_types = NULL;
    test_completed = false;

    // Every type has its own detail page and header.
    const char* headings[] = {
        "Reminder", "Meeting notes", "Scan results", "Project update", "Studio crit",
        "Sam Lee", "Blue Bottle Coffee", "Home to Work",
    };
    const char* leads[] = {
        DETAIL_LEAD, "", "Dr Okafor", "kim@example.com", "Sep 5, 2026, 10:00 AM - 11:00 AM",
        "Designer · Acme", "66 Mint St", "24 mins · 18 km",
    };
    char detail_id[32];
    for (size_t t = 0; t < SPARK_ITEM_CARD; ++t) {
        snprintf(detail_id, sizeof(detail_id), "detail-%zu", t);
        test_display_id = detail_id;
        test_type = (spark_item_type_t)t;
        send_request("1", 1, false, NULL, 0);
        assert(last_error == Dp_ErrNone && spark_display_current()->items[0].type == test_type);
        assert(strcmp(lv_label_get_text(title_label), headings[t]) == 0);
        assert(strcmp(lv_label_get_text(lead), leads[t]) == 0);
        lv_refr_now(display);
        char frame_name[32]; snprintf(frame_name, sizeof(frame_name), "detail-%s", type_names[t]);
        save_frame(artifacts, frame_name);
    }
    assert(strcmp(lv_label_get_text(body), "via I-280") == 0);
    test_type = SPARK_ITEM_TODO;

    // Screen off under three minutes keeps the page. Over three minutes the glasses
    // clear it as a local dismiss: identity and revision survive, and it is reported.
    // Screen state reaches Spark through the system listener, whichever page is current.
    test_display_id = "titled-again";
    send_request("1", 1, false, NULL, 0); assert(last_error == Dp_ErrNone);
    assert(sys_state_listener != NULL);
    test_now_ms = 1000;
    sys_state_listener(0);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS - 1;
    unsigned before_dismiss = reports;
    sys_state_listener(1);
    assert(spark_display_current() != NULL && reports == before_dismiss);
    sys_state_listener(0);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS;
    send_request("2", -1, true, "Still here", 0); assert(last_error == Dp_ErrNone);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS - 1;
    sys_state_listener(1);
    assert(spark_display_current() != NULL && reports == before_dismiss); // Content in the dark restarts the clock.
    assert(strcmp(lv_label_get_text(reply_label), "Still here") == 0);
    sys_state_listener(0);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS;
    flushed_pixels = 0;
    sys_state_listener(1);
    assert(spark_display_current() == NULL && lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN));
    assert(flushed_pixels > 0); // The clear paints before the screen-on refresh.
    assert(reports == before_dismiss + 1 && strcmp(last_command, "dismissed") == 0);
    assert(strcmp(last_display_id, "titled-again") == 0 && strcmp(last_revision, "2") == 0);
    assert(lv_label_get_text(reply_label)[0] == '\0');
    uint64_t dismissed_sequence = last_navigation_sequence;
    assert(dismissed_sequence > test_navigation_sequence);
    sys_state_listener(0);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS;
    sys_state_listener(1);
    assert(reports == before_dismiss + 1); // Nothing to clear, nothing to report.
    // The phone missed the report: any update echoing the older sequence is ACKed and answered
    // with the actual view, so the mirror heals on the next exchange.
    send_request("3", -1, true, "After clear", 0);
    assert(last_error == Dp_ErrNone && strcmp(lv_label_get_text(reply_label), "After clear") == 0);
    assert(reports == before_dismiss + 2 && strcmp(last_command, "pageDismissed") == 0);
    assert(last_navigation_sequence > dismissed_sequence && strcmp(last_revision, "3") == 0);
    dismissed_sequence = last_navigation_sequence;
    send_request("4", 1, false, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current() == NULL);
    assert(reports == before_dismiss + 3 && strcmp(last_command, "pageDismissed") == 0);
    assert(last_navigation_sequence > dismissed_sequence);
    // A phone that saw the clear echoes the current sequence; its list applies quietly.
    test_navigation_sequence = last_navigation_sequence;
    before_dismiss = reports;
    send_request("5", 2, true, NULL, 0);
    assert(last_error == Dp_ErrNone && spark_display_current() != NULL && reports == before_dismiss);
    // Host disconnect clears everything, revision included. A reconnecting phone must
    // start a new display; the old identity is gone.
    uint8_t host_connected = 1, host_disconnected = 0;
    uint32_t btconn_event = system_runtime_state_get_btconn_event();
    lv_obj_send_event(parent, btconn_event, &host_connected);
    assert(spark_display_current() != NULL);
    lv_obj_send_event(parent, btconn_event, &host_disconnected);
    assert(spark_display_current() == NULL && spark_assistant_current()->state == SPARK_ASSISTANT_IDLE);
    assert(reports == before_dismiss);
    send_request("6", -1, true, "No page", 0); assert(last_error == ErrBadParam);
    test_display_id = "after-disconnect";
    send_request("1", 1, false, "Back", 0);
    assert(last_error == Dp_ErrNone && spark_display_current() != NULL);
    // A phone dismiss clears the page and reply on screen, then turns the screen off.
    // The ACK follows the paint and the screen change; a retry toggles nothing.
    assert(sys_state_sets == 0);
    test_screen = "on";
    send_request("2", 0, true, "", 0); assert(last_error == ErrBadParam);
    test_screen = NULL; test_screen_bad_type = true;
    send_request("2", 0, true, "", 0); assert(last_error == ErrBadParam);
    test_screen_bad_type = false;
    // The dismiss must be explicit: an empty page and an empty reply, nothing else.
    test_screen = "off";
    send_request("2", 1, false, NULL, 0); assert(last_error == ErrBadParam);
    send_request("2", -1, true, "", 0); assert(last_error == ErrBadParam);
    send_request("2", 1, true, "", 0); assert(last_error == ErrBadParam);
    send_request("2", 0, true, "Kept", 0); assert(last_error == ErrBadParam);
    assert(sys_state_sets == 0 && spark_display_current()->count != 0);
    flushed_pixels = 0;
    send_request("2", 0, true, "", 0);
    assert(last_error == Dp_ErrNone && spark_display_current()->count == 0);
    assert(lv_obj_has_flag(frame, LV_OBJ_FLAG_HIDDEN) && lv_label_get_text(reply_label)[0] == '\0');
    assert(flushed_at_ack > 0 && sys_state_sets == 1 && test_sys_state == 0);
    assert(strcmp(last_sys_state_trigger, "phoneDismiss") == 0);
    send_request("2", 0, true, "", 0);
    assert(last_error == Dp_ErrNone && sys_state_sets == 1);
    test_screen = NULL;
    // Content sent while the screen is off waits for the next screen on, then survives it.
    sys_state_listener(0);
    send_request("3", 2, true, "Woke", 0); assert(last_error == Dp_ErrNone);
    test_now_ms += SPARK_SCREEN_OFF_CLEAR_MS - 1;
    sys_state_listener(1);
    assert(spark_display_current()->count == 2 && strcmp(lv_label_get_text(reply_label), "Woke") == 0);
    active_app = "prompter";
    send_request("7", 1, false, NULL, 0); assert(last_error == ErrNotReady);
    app->on_stop(); spark_page_get()->on_destroy(); lv_obj_delete(parent);
    lv_display_delete(display); lv_deinit();
    puts("Spark display: typed items, per-type cards and details, strict items, partial redraw, five-row fit, independent updates, retry ACK, validation, ownership, layout, reports, screen-off clear, phone dismiss and disconnect passed.");
}
