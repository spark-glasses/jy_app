#include "spark.h"
#include "assistant_avatar.h"

#include "floatair_dbg.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/font/lv_binfont_loader.h"
#include "system/system_res.h"
#include "system/system_runtime_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPARK_FRAME_MARGIN_H 8
#define SPARK_FRAME_MARGIN_V 4
#define SPARK_FRAME_RADIUS 20
#define SPARK_FRAME_INSET_H 24
#define SPARK_HEADER_HEIGHT 64
#define SPARK_LIST_HEADER_HEIGHT 44
#define SPARK_LINE_HEIGHT 24
#define SPARK_CARD_PADDING 8
#define SPARK_FOOTER_HEIGHT 72
#define SPARK_AVATAR_LEFT 30
#define SPARK_AVATAR_SIZE 40
#define SPARK_AVATAR_BOTTOM 16
#define SPARK_REPLY_LEFT 91
#define SPARK_REPLY_RIGHT 24
#define SPARK_REPLY_HEIGHT 56
#define SPARK_REPLY_BOTTOM 8
#define SPARK_REPLY_PADDING_H 12
#define SPARK_REPLY_PADDING_V 8
#define SPARK_REPLY_BORDER_WIDTH 1
#define SPARK_REPLY_LINE_SPACE 2
#define SPARK_REPLY_FONT_SIZE 12
#define SPARK_REPLY_MAX_LINES 2
#define SPARK_REPLY_FONT_PATH "A:/romfs/system/font/open_runde_12.bin"

typedef struct {
    lv_obj_t* root;
    lv_obj_t* mark;
    lv_obj_t* primary;
    lv_obj_t* secondary;
    lv_obj_t* meta;
    lv_obj_t* address;
    lv_obj_t* subject;
} spark_row_t;

static spark_display_t* s_display;
static spark_display_t* s_return_list;
static lv_obj_t* s_frame;
static lv_obj_t* s_header;
static lv_obj_t* s_title;
static lv_obj_t* s_hint;
static lv_obj_t* s_pages;
static lv_obj_t* s_content;
static lv_obj_t* s_lead;
static lv_obj_t* s_body;
static lv_obj_t* s_meta;
static lv_obj_t* s_footer;
static spark_assistant_avatar_t* s_avatar;
static lv_obj_t* s_reply_panel;
static lv_obj_t* s_reply_label;
static lv_font_t* s_reply_font;
static spark_row_t s_rows[SPARK_DISPLAY_VISIBLE_ROWS];
static bool s_ready;
static void set_header_height(int32_t height) {
    lv_obj_set_height(s_header, height);
    lv_obj_set_pos(s_content, 0, height + 6);
    int32_t available = lv_obj_get_content_height(s_frame) - height - 8;
    lv_obj_set_size(s_content, LV_PCT(100), LV_MAX(1,
        height == SPARK_LIST_HEADER_HEIGHT ? LV_MIN(available, SPARK_DISPLAY_CONTENT_HEIGHT) : available));
}

static int32_t text_width(lv_obj_t* label, const char* text) {
    lv_point_t size;
    lv_text_get_size(&size, text, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static void line(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width) {
    // DOT mode can modify its buffer. Give LVGL an owned copy, never a model string.
    lv_label_set_text(label, text == NULL ? "" : text);
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, LV_MAX(1, width), SPARK_LINE_HEIGHT);
    lv_obj_update_flag(label, LV_OBJ_FLAG_HIDDEN, width <= 0 || text == NULL || text[0] == '\0');
}

static void select_row(spark_row_t* row, bool selected, bool done) {
    lv_color_t primary = lv_color_hex(done ? 0x888888 : selected ? 0xffffff : 0xb8b8b8);
    lv_color_t secondary = lv_color_hex(selected ? 0xb8b8b8 : 0x888888);
    lv_obj_set_style_text_color(row->primary, primary, LV_PART_MAIN);
    lv_obj_set_style_text_color(row->subject, primary, LV_PART_MAIN);
    lv_obj_set_style_text_color(row->secondary, secondary, LV_PART_MAIN);
    lv_obj_set_style_text_color(row->meta, secondary, LV_PART_MAIN);
    lv_obj_set_style_text_color(row->address, secondary, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row->root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(row->root, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(row->mark, primary, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row->mark, primary, LV_PART_MAIN);
}

static void render_list_item(spark_row_t* row, const spark_display_row_t* item, int32_t y, bool selected) {
    lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(row->root, LV_PCT(100), item->height);
    lv_obj_set_y(row->root, y);
    lv_obj_update_layout(row->root);
    int32_t width = lv_obj_get_content_width(row->root);
    bool reminder = item->layout == SPARK_LAYOUT_REMINDER;
    bool email = item->layout == SPARK_LAYOUT_EMAIL;
    bool done = reminder && strcmp(item->mark, "[x]") == 0;
    lv_obj_update_flag(row->mark, LV_OBJ_FLAG_HIDDEN, !reminder);
    lv_obj_set_style_bg_opa(row->mark, done ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    int32_t x = reminder ? 26 : 0;
    int32_t meta_width = (reminder || email) && item->meta[0] != '\0'
        ? LV_MIN(text_width(row->meta, item->meta), width / 3) : 0;
    int32_t primary_width = width - x - (meta_width == 0 ? 0 : meta_width + 10);
    int32_t name_width = email ? LV_MIN(primary_width, text_width(row->primary, item->primary)) : primary_width;
    line(row->primary, item->primary, x, 0, name_width);
    line(row->address, email ? item->address : NULL, name_width + 8, 2, primary_width - name_width - 8);
    if (reminder || email) line(row->meta, item->meta, width - meta_width, 2, meta_width);
    else line(row->meta, item->layout == SPARK_LAYOUT_EVENT ? item->meta : NULL, 0, 2 * SPARK_LINE_HEIGHT, width);

    if (email && item->subject != NULL && item->subject[0] != '\0') {
        int32_t subject_width = LV_MIN(width, text_width(row->subject, item->subject));
        line(row->subject, item->subject, 0, SPARK_LINE_HEIGHT, subject_width);
        const char* preview = item->secondary + strlen(item->subject);
        line(row->secondary, preview, subject_width, SPARK_LINE_HEIGHT, width - subject_width);
    } else {
        line(row->subject, NULL, 0, 0, 0);
        line(row->secondary, reminder ? NULL : item->secondary, 0, SPARK_LINE_HEIGHT, width);
    }
    select_row(row, selected, done);
}

static void render_header(const spark_display_t* display) {
    int32_t width = lv_obj_get_content_width(s_header);
    bool list = display->is_list;
    char pages[48] = "";
    if (list && display->page_count > 1)
        snprintf(pages, sizeof(pages), "%s%u/%u%s", display->page_index > 1 ? "< " : "",
                 (unsigned)display->page_index, (unsigned)display->page_count,
                 display->page_index < display->page_count ? " >" : "");
    int32_t page_width = pages[0] == '\0' ? 0 : LV_MIN(text_width(s_pages, pages), width / 3);
    line(s_pages, pages, width - page_width, 10, page_width);
    int32_t available = width - (page_width ? page_width + 12 : 0);
    int32_t title_width = list ? LV_MIN(text_width(s_title, display->title), available * 2 / 3) : available;
    line(s_title, display->title, 0, 8, title_width);
    lv_obj_set_height(s_title, 32);
    const lv_font_t* title_font = lv_obj_get_style_text_font(s_title, LV_PART_MAIN);
    const lv_font_t* hint_font = lv_obj_get_style_text_font(s_hint, LV_PART_MAIN);
    int32_t hint_y = 8 + title_font->line_height - title_font->base_line
                       - hint_font->line_height + hint_font->base_line;
    if (list) line(s_hint, display->hint, title_width + 10, hint_y, available - title_width - 10);
    else line(s_hint, display->hint, 0, 38, width);
}

static void render_full_item(const spark_display_t* display) {
    const spark_display_row_t* row = &display->rows[0];
    lv_label_set_text_static(s_lead, row->primary);
    lv_label_set_text_static(s_body, row->secondary);
    lv_label_set_text_static(s_meta, row->meta);
    lv_obj_update_layout(s_lead);
    lv_obj_set_y(s_body, row->primary[0] == '\0' ? 8 :
                 lv_obj_get_y(s_lead) + lv_obj_get_height(s_lead) + 12);
    lv_obj_update_layout(s_body);
    lv_obj_set_y(s_meta, lv_obj_get_y(s_body) + lv_obj_get_height(s_body) + 12);
}

static void spark_render(const spark_display_t* display) {
    if (!s_ready) return;
    bool visible = display != NULL && display->count != 0;
    bool is_list = visible && display->is_list;
    set_header_height(is_list ? SPARK_LIST_HEADER_HEIGHT : SPARK_HEADER_HEIGHT);
    lv_label_set_text_static(s_lead, "");
    lv_label_set_text_static(s_body, "");
    lv_label_set_text_static(s_meta, "");
    lv_obj_add_flag(s_lead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_static(s_title, "");
    lv_label_set_text_static(s_hint, "");
    for (size_t i = 0; i < SPARK_DISPLAY_VISIBLE_ROWS; ++i)
        lv_obj_add_flag(s_rows[i].root, LV_OBJ_FLAG_HIDDEN);
    if (!visible) {
        lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    render_header(display);
    if (is_list) {
        lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
        int32_t y = 0;
        size_t page = display->page_index - 1;
        size_t start = display->page_starts[page];
        size_t end = page + 1 < display->page_count ? display->page_starts[page + 1] : display->count;
        for (size_t i = start; i < end; ++i) {
            render_list_item(&s_rows[i - start], &display->rows[i], y, i == display->selected);
            y += display->rows[i].height + display->row_gap;
        }
    } else {
        lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_lead, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
        render_full_item(display);
    }
    lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_frame);
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    lv_obj_invalidate(s_frame);
}

const spark_display_t* spark_display_current(void) {
    return s_display;
}

static bool same_text(const char* a, const char* b) {
    return strcmp(a == NULL ? "" : a, b == NULL ? "" : b) == 0;
}

static bool same_list(const spark_display_t* a, const spark_display_t* b) {
    if (a == NULL || !a->is_list || !b->is_list || a->count != b->count ||
        a->page_count != b->page_count || a->row_gap != b->row_gap ||
        !same_text(a->title, b->title) || !same_text(a->hint, b->hint)) return false;
    if (memcmp(a->page_starts, b->page_starts, sizeof(a->page_starts)) != 0) return false;
    for (size_t i = 0; i < a->count; ++i) {
        const spark_display_row_t* x = &a->rows[i];
        const spark_display_row_t* y = &b->rows[i];
        if (x->layout != y->layout || x->height != y->height || !same_text(x->id, y->id) ||
            !same_text(x->mark, y->mark) || !same_text(x->primary, y->primary) ||
            !same_text(x->secondary, y->secondary) || !same_text(x->meta, y->meta) ||
            !same_text(x->address, y->address) || !same_text(x->subject, y->subject)) return false;
    }
    return true;
}

static void spark_select(size_t next) {
    size_t old = s_display->selected;
    if (old == next) return;
    size_t old_page = s_display->page_index - 1, page = 0;
    while (page + 1 < s_display->page_count && next >= s_display->page_starts[page + 1]) ++page;
    s_display->selected = next;
    s_display->page_index = page + 1;
    if (page != old_page) {
        spark_render(s_display);
    } else {
        size_t start = s_display->page_starts[page];
        select_row(&s_rows[old - start], false,
            s_display->rows[old].layout == SPARK_LAYOUT_REMINDER && strcmp(s_display->rows[old].mark, "[x]") == 0);
        select_row(&s_rows[next - start], true,
            s_display->rows[next].layout == SPARK_LAYOUT_REMINDER && strcmp(s_display->rows[next].mark, "[x]") == 0);
    }
    system_ui_request_screen_refresh();
}

bool spark_display_is_selected(const char* id) {
    return s_display != NULL && s_display->count != 0 &&
        strcmp(s_display->rows[s_display->selected].id, id) == 0;
}

bool spark_display_apply(spark_display_t* display, bool new_display) {
    if (!s_ready || display == NULL) return false;
    if (same_list(s_display, display)) {
        spark_select(display->selected);
        spark_display_free(display);
        return true;
    }
    spark_display_t* old = s_display;
    if (new_display || display->is_list) {
        spark_display_free(s_return_list);
        s_return_list = NULL;
    } else if (old != NULL && old->is_list) {
        s_return_list = old;
        old = NULL;
    }
    spark_render(display);
    s_display = display;
    spark_display_free(old);
    system_ui_request_screen_refresh();
    return true;
}

bool spark_display_ready(void) { return s_ready; }

void spark_display_clear(void) {
    spark_display_reset_revision();
    (void)spark_assistant_apply(spark_assistant_current());
    (void)spark_reply_set("");
    spark_render(NULL);
    spark_display_free(s_display);
    s_display = NULL;
    spark_display_free(s_return_list);
    s_return_list = NULL;
    system_ui_request_screen_refresh();
}

static char* copy_text(const char* text) {
    if (text == NULL) text = "";
    size_t size = strlen(text) + 1;
    char* copy = malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static spark_display_t* spark_item_from_selected(void) {
    const spark_display_row_t* src = &s_display->rows[s_display->selected];
    spark_display_t* item = calloc(1, sizeof(*item));
    if (item == NULL) return NULL;
    item->count = 1;
    bool reminder = src->layout == SPARK_LAYOUT_REMINDER;
    item->title = copy_text(reminder || src->primary[0] == '\0' ? s_display->title : src->primary);
    item->hint = copy_text("");
    spark_display_row_t* row = &item->rows[0];
    row->id = copy_text(src->id);
    row->mark = copy_text("");
    row->primary = copy_text(reminder ? src->primary : "");
    row->secondary = copy_text(src->secondary);
    row->meta = copy_text(src->meta);
    if (item->title == NULL || item->hint == NULL || row->id == NULL || row->id[0] == '\0' ||
        row->mark == NULL || row->primary == NULL || row->secondary == NULL || row->meta == NULL) {
        spark_display_free(item);
        return NULL;
    }
    return item;
}

static void spark_prepare_static_obj(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE |
                                LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
}

static const lv_font_t* spark_reply_font(void) {
    if (s_reply_font == NULL) {
        s_reply_font = lv_binfont_create(SPARK_REPLY_FONT_PATH);
        if (s_reply_font != NULL) {
            s_reply_font->fallback = get_font_by_size_near(SPARK_REPLY_FONT_SIZE);
        }
    }
    return s_reply_font != NULL ? s_reply_font : get_font_by_size_near(SPARK_REPLY_FONT_SIZE);
}

static void spark_input(lv_event_t* event) {
    if (s_display == NULL || s_display->count == 0) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_GESTURE_LEFT || code == LV_EVENT_GESTURE_RIGHT) {
        if (s_display->is_list) {
            size_t next = s_display->selected;
            if (code == LV_EVENT_GESTURE_LEFT && next + 1 < s_display->count) ++next;
            if (code == LV_EVENT_GESTURE_RIGHT && next > 0) --next;
            spark_select(next);
            spark_display_report("selected", s_display->rows[next].id);
        } else {
            int32_t step = lv_obj_get_content_height(s_content) / 2;
            lv_obj_scroll_by(s_content, 0, code == LV_EVENT_GESTURE_LEFT ? -step : step, LV_ANIM_OFF);
            system_ui_request_screen_refresh();
        }
    } else if (code == LV_EVENT_CLICKED && s_display->is_list) {
        const char* id = s_display->rows[s_display->selected].id;
        spark_display_t* item = spark_item_from_selected();
        spark_display_report("open", id);
        if (item == NULL || !spark_display_apply(item, false)) spark_display_free(item);
    } else if (code == LV_EVENT_DCLICKED && !s_display->is_list) {
        if (s_return_list == NULL) return;
        spark_display_t* old = s_display;
        s_display = s_return_list;
        s_return_list = NULL;
        spark_render(s_display);
        spark_display_free(old);
        system_ui_request_screen_refresh();
        spark_display_report("selected", s_display->rows[s_display->selected].id);
    }
}

static void spark_page_create(lv_obj_t* parent, const app_page_data_t* data) {
    (void)data;
    lv_obj_add_event_cb(parent, spark_input, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(parent);

    lv_obj_t* frame = lv_obj_create(parent);
    floatair_assert(frame != NULL, "Spark display frame create failed");
    if (frame == NULL) return;

    s_frame = frame;
    spark_prepare_static_obj(frame);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(frame, get_font_by_size_near(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(frame, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_size(
        frame,
        LV_MAX(1, lv_obj_get_content_width(parent) - SPARK_FRAME_MARGIN_H * 2),
        LV_MAX(1, lv_obj_get_content_height(parent) - SPARK_FOOTER_HEIGHT -
                      SPARK_FRAME_MARGIN_V * 2));
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, SPARK_FRAME_MARGIN_V);
    lv_obj_set_style_border_color(frame, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(frame, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(frame, SPARK_FRAME_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(frame, SPARK_FRAME_INSET_H, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(frame, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(frame, true, LV_PART_MAIN);

    s_header = lv_obj_create(frame);
    floatair_assert(s_header != NULL, "Spark display header create failed");
    if (s_header == NULL) return;

    spark_prepare_static_obj(s_header);
    lv_obj_set_size(s_header, LV_PCT(100), SPARK_HEADER_HEIGHT);
    lv_obj_align(s_header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_border_color(s_header, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_header, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    s_title = lv_label_create(s_header);
    s_hint = lv_label_create(s_header);
    s_pages = lv_label_create(s_header);
    s_content = lv_obj_create(frame);
    if (s_title == NULL || s_hint == NULL || s_pages == NULL || s_content == NULL) return;
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_pages, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_title, get_font_by_size_near(24), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_pages, get_font_by_size_near(16), LV_PART_MAIN);
    lv_obj_set_size(s_title, LV_PCT(100), 40);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_set_style_text_font(s_hint, get_font_by_size_near(14), LV_PART_MAIN);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    spark_prepare_static_obj(s_content);
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(frame);
    lv_obj_set_size(s_content, LV_PCT(100), LV_MAX(1, lv_obj_get_content_height(frame) - SPARK_HEADER_HEIGHT));
    lv_obj_set_pos(s_content, 0, SPARK_HEADER_HEIGHT);
    s_lead = lv_label_create(s_content);
    s_body = lv_label_create(s_content);
    s_meta = lv_label_create(s_content);
    if (s_lead == NULL || s_body == NULL || s_meta == NULL) return;
    lv_label_set_long_mode(s_lead, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lead, LV_PCT(100));
    lv_obj_set_y(s_lead, 8);
    lv_obj_set_style_text_font(s_lead, get_font_by_size_near(20), LV_PART_MAIN);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_body, LV_PCT(100));
    lv_obj_set_y(s_body, 48);
    lv_obj_set_style_text_line_space(s_body, 4, LV_PART_MAIN);
    lv_label_set_long_mode(s_meta, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_meta, LV_PCT(100));
    lv_obj_set_style_text_font(s_meta, get_font_by_size_near(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_meta, lv_color_make(170, 170, 170), LV_PART_MAIN);
    lv_obj_update_layout(s_content);
    for (size_t i = 0; i < SPARK_DISPLAY_VISIBLE_ROWS; ++i) {
        spark_row_t* row = &s_rows[i];
        row->root = lv_obj_create(s_content);
        if (row->root == NULL) return;
        spark_prepare_static_obj(row->root);
        lv_obj_set_size(row->root, LV_PCT(100), 64);
        lv_obj_set_style_bg_opa(row->root, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->root, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(row->root, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row->root, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row->root, SPARK_CARD_PADDING, LV_PART_MAIN);
        row->mark = lv_obj_create(row->root);
        spark_prepare_static_obj(row->mark);
        lv_obj_set_size(row->mark, 16, 16);
        lv_obj_set_pos(row->mark, 0, 4);
        lv_obj_set_style_border_width(row->mark, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row->mark, 3, LV_PART_MAIN);
        row->primary = lv_label_create(row->root);
        row->secondary = lv_label_create(row->root);
        row->meta = lv_label_create(row->root);
        row->address = lv_label_create(row->root);
        row->subject = lv_label_create(row->root);
        lv_obj_t* labels[] = {row->primary, row->secondary, row->meta, row->address, row->subject};
        const unsigned sizes[] = {20, 18, 16, 16, 20};
        for (size_t j = 0; j < 5; ++j) {
            if (labels[j] == NULL) return;
            lv_label_set_long_mode(labels[j], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(labels[j], get_font_by_size_near(sizes[j]), LV_PART_MAIN);
        }
    }

    s_footer = lv_obj_create(parent);
    if (s_footer == NULL) return;
    spark_prepare_static_obj(s_footer);
    lv_obj_set_size(s_footer, LV_PCT(100), SPARK_FOOTER_HEIGHT);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_avatar = spark_assistant_avatar_create(s_footer, SPARK_AVATAR_SIZE);
    lv_obj_t* avatar_obj = spark_assistant_avatar_object(s_avatar);
    if (s_avatar == NULL || avatar_obj == NULL) return;
    lv_obj_align(avatar_obj, LV_ALIGN_BOTTOM_LEFT, SPARK_AVATAR_LEFT, -SPARK_AVATAR_BOTTOM);

    s_reply_panel = lv_obj_create(s_footer);
    s_reply_label = s_reply_panel != NULL ? lv_label_create(s_reply_panel) : NULL;
    if (s_reply_panel == NULL || s_reply_label == NULL) return;
    lv_obj_set_size(s_reply_panel, 1, SPARK_REPLY_HEIGHT);
    lv_obj_align(s_reply_panel, LV_ALIGN_BOTTOM_LEFT, SPARK_REPLY_LEFT, -SPARK_REPLY_BOTTOM);
    lv_obj_set_style_bg_color(s_reply_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_reply_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_reply_panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_reply_panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_reply_panel, SPARK_REPLY_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_radius(s_reply_panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_reply_panel, SPARK_REPLY_PADDING_H, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_reply_panel, SPARK_REPLY_PADDING_V, LV_PART_MAIN);
    lv_obj_remove_flag(s_reply_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                          LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_line_space(s_reply_label, SPARK_REPLY_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_size(s_reply_label, 1, 1);
    lv_label_set_long_mode(s_reply_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_reply_label, spark_reply_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_reply_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_reply_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_reply_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_reply_label, "");
    lv_obj_add_flag(s_reply_panel, LV_OBJ_FLAG_HIDDEN);
    spark_assistant_avatar_roll_in(s_avatar);

    s_ready = true;
    spark_render(s_display);
}

static void spark_page_destroy(void) {
    s_ready = false;
    s_frame = s_header = s_title = s_hint = s_pages = s_content = s_lead = s_body = s_meta = NULL;
    s_footer = NULL;
    s_avatar = NULL;
    s_reply_panel = s_reply_label = NULL;
    memset(s_rows, 0, sizeof(s_rows));
}

static app_page_t s_spark_page = {
    .name = "spark",
    .on_create = spark_page_create,
    .on_destroy = spark_page_destroy,
    .on_unload = spark_display_clear,
};

app_page_t* spark_page_get(void) {
    return &s_spark_page;
}

bool spark_reply_set(const char* text) {
    if (!s_ready || s_reply_panel == NULL || s_reply_label == NULL ||
        !lv_obj_is_valid(s_reply_panel) || !lv_obj_is_valid(s_reply_label)) {
        return false;
    }

    if (text == NULL || text[0] == '\0') {
        lv_label_set_text(s_reply_label, "");
        lv_obj_add_flag(s_reply_panel, LV_OBJ_FLAG_HIDDEN);
        system_ui_request_screen_refresh();
        return true;
    }

    lv_label_set_text(s_reply_label, text);
    const lv_font_t* font = lv_obj_get_style_text_font(s_reply_label, LV_PART_MAIN);
    lv_coord_t max_panel_width = LV_MAX(
        1, lv_obj_get_content_width(s_footer) - SPARK_REPLY_LEFT - SPARK_REPLY_RIGHT);
    lv_coord_t insets = 2 * (SPARK_REPLY_PADDING_H + SPARK_REPLY_BORDER_WIDTH);
    lv_coord_t max_text_width = LV_MAX(1, max_panel_width - insets);
    lv_point_t size = {0, 0};
    lv_text_get_size(
        &size, text, font, 0, SPARK_REPLY_LINE_SPACE,
        max_text_width, LV_TEXT_FLAG_NONE);
    lv_coord_t panel_width = LV_MIN(max_panel_width, LV_MAX(1, size.x) + insets);
    lv_obj_set_width(s_reply_panel, panel_width);
    lv_obj_set_height(s_reply_panel, SPARK_REPLY_HEIGHT);
    lv_obj_update_layout(s_reply_panel);
    lv_coord_t label_width = LV_MAX(1, lv_obj_get_content_width(s_reply_panel));
    lv_coord_t content_height = LV_MAX(1, lv_obj_get_content_height(s_reply_panel));
    lv_coord_t two_line_height =
        (lv_coord_t)lv_font_get_line_height(font) * SPARK_REPLY_MAX_LINES +
        SPARK_REPLY_LINE_SPACE * (SPARK_REPLY_MAX_LINES - 1);
    lv_text_get_size(
        &size, text, font, 0, SPARK_REPLY_LINE_SPACE,
        label_width, LV_TEXT_FLAG_NONE);
    lv_obj_set_size(
        s_reply_label,
        label_width,
        LV_MIN(content_height, LV_MIN(two_line_height, LV_MAX(1, size.y))));
    lv_obj_align(s_reply_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(s_reply_panel, LV_OBJ_FLAG_HIDDEN);
    system_ui_request_screen_refresh();
    return true;
}

bool spark_assistant_apply(const spark_assistant_presentation_t* presentation) {
    if (!s_ready || s_avatar == NULL || presentation == NULL) return false;
    return spark_assistant_avatar_set_state(s_avatar, presentation);
}
