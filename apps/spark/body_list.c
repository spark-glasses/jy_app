#include "view_internal.h"

#include <string.h>

typedef struct {
    lv_obj_t* root;
    lv_obj_t* mark;
    lv_obj_t* primary;
    lv_obj_t* secondary;
    lv_obj_t* meta;
    lv_obj_t* address;
    lv_obj_t* subject;
} spark_row_t;

static spark_row_t s_rows[SPARK_DISPLAY_VISIBLE_ROWS];

static bool row_done(const spark_display_row_t* item) {
    return item->layout == SPARK_LAYOUT_REMINDER && strcmp(spark_text(item->mark), "[x]") == 0;
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

static void render_item(spark_row_t* row, const spark_display_row_t* item, int32_t y, bool selected) {
    bool note = item->layout == SPARK_LAYOUT_NOTE;
    bool email = item->layout == SPARK_LAYOUT_EMAIL;
    bool compact = email || item->layout == SPARK_LAYOUT_EVENT;
    lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(row->root, LV_PCT(100), item->height);
    lv_obj_set_style_pad_ver(row->root, note ? 4 : compact ? 6 : SPARK_CARD_PADDING, LV_PART_MAIN);
    lv_obj_set_y(row->root, y);
    lv_obj_update_layout(row->root);
    lv_obj_set_style_text_font(row->primary, spark_font(note ? 16 : 18), LV_PART_MAIN);
    lv_obj_set_style_text_font(row->secondary, spark_font(note ? 14 : 16), LV_PART_MAIN);
    lv_obj_set_style_text_font(row->meta, spark_font(note ? 12 : 14), LV_PART_MAIN);
    int32_t width = lv_obj_get_content_width(row->root);
    bool reminder = item->layout == SPARK_LAYOUT_REMINDER;
    bool done = row_done(item);
    lv_obj_update_flag(row->mark, LV_OBJ_FLAG_HIDDEN, !reminder);
    lv_obj_set_style_bg_opa(row->mark, done ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (note) {
        int32_t date_width = spark_text_empty(item->meta)
            ? 0 : LV_MIN(spark_text_width(row->meta, item->meta), width / 3);
        int32_t preview_x = date_width == 0 ? 0 : date_width + 10;
        spark_line(row->primary, item->primary, 0, 0, width);
        spark_line(row->meta, item->meta, 0, 20, date_width);
        spark_line(row->secondary, item->secondary, preview_x, 20, width - preview_x);
        spark_line(row->address, NULL, 0, 0, 0);
        spark_line(row->subject, NULL, 0, 0, 0);
        select_row(row, selected, false);
        return;
    }
    int32_t x = reminder ? 26 : 0;
    int32_t meta_width = (reminder || email) && !spark_text_empty(item->meta)
        ? LV_MIN(spark_text_width(row->meta, item->meta), width / 3) : 0;
    int32_t primary_width = width - x - (meta_width == 0 ? 0 : meta_width + 10);
    int32_t name_width = email ? LV_MIN(primary_width, spark_text_width(row->primary, item->primary)) : primary_width;
    spark_line(row->primary, item->primary, x, 0, name_width);
    spark_line(row->address, email ? item->address : NULL, name_width + 8, 2, primary_width - name_width - 8);
    if (reminder || email) spark_line(row->meta, item->meta, width - meta_width, 2, meta_width);
    else spark_line(row->meta, item->layout == SPARK_LAYOUT_EVENT ? item->meta : NULL, 0, 2 * SPARK_LINE_HEIGHT, width);

    if (email && !spark_text_empty(item->subject)) {
        int32_t subject_width = LV_MIN(width, spark_text_width(row->subject, item->subject));
        spark_line(row->subject, item->subject, 0, SPARK_LINE_HEIGHT, subject_width);
        const char* preview = item->secondary + strlen(item->subject);
        spark_line(row->secondary, preview, subject_width, SPARK_LINE_HEIGHT, width - subject_width);
    } else {
        spark_line(row->subject, NULL, 0, 0, 0);
        spark_line(row->secondary, reminder ? NULL : item->secondary, 0, SPARK_LINE_HEIGHT, width);
    }
    select_row(row, selected, done);
}

static bool create(lv_obj_t* content) {
    for (size_t i = 0; i < SPARK_DISPLAY_VISIBLE_ROWS; ++i) {
        spark_row_t* row = &s_rows[i];
        row->root = lv_obj_create(content);
        if (row->root == NULL) return false;
        spark_prepare_static_obj(row->root);
        lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(row->root, LV_PCT(100), 64);
        lv_obj_set_style_bg_opa(row->root, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row->root, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(row->root, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(row->root, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row->root, SPARK_CARD_PADDING, LV_PART_MAIN);
        row->mark = lv_obj_create(row->root);
        if (row->mark == NULL) return false;
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
        const unsigned sizes[] = {18, 16, 14, 14, 18};
        for (size_t j = 0; j < 5; ++j) {
            if (labels[j] == NULL) return false;
            lv_label_set_long_mode(labels[j], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(labels[j], spark_font(sizes[j]), LV_PART_MAIN);
        }
    }
    return true;
}

static void reset(void) {
    for (size_t i = 0; i < SPARK_DISPLAY_VISIBLE_ROWS; ++i)
        lv_obj_add_flag(s_rows[i].root, LV_OBJ_FLAG_HIDDEN);
}

static void render(const spark_display_t* display) {
    int32_t y = 0;
    size_t page = display->page_index - 1;
    size_t start = display->page_starts[page];
    size_t end = page + 1 < display->page_count ? display->page_starts[page + 1] : display->count;
    for (size_t i = start; i < end; ++i) {
        render_item(&s_rows[i - start], &display->rows[i], y, i == display->selected);
        y += display->rows[i].height + display->row_gap;
    }
}

static void destroy(void) {
    memset(s_rows, 0, sizeof(s_rows));
}

void spark_body_list_select(size_t page_row, const spark_display_row_t* row, bool selected) {
    select_row(&s_rows[page_row], selected, row_done(row));
}

const spark_body_t spark_body_list = {create, reset, render, destroy};
