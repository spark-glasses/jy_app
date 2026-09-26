#include "view_internal.h"

#include <stdlib.h>
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

static bool item_done(const spark_display_item_t* item) {
    return item->type == SPARK_ITEM_TODO && spark_item_flag(item, "completed");
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

static void style_row(spark_row_t* row, unsigned pad, unsigned primary, unsigned secondary,
                      unsigned meta) {
    lv_obj_set_style_pad_ver(row->root, pad, LV_PART_MAIN);
    lv_obj_update_layout(row->root);
    lv_obj_set_style_text_font(row->primary, spark_font(primary), LV_PART_MAIN);
    lv_obj_set_style_text_font(row->secondary, spark_font(secondary), LV_PART_MAIN);
    lv_obj_set_style_text_font(row->meta, spark_font(meta), LV_PART_MAIN);
}

static void hide(lv_obj_t* label) {
    spark_line(label, NULL, 0, 0, 0);
}

// One line with a checkbox in front and a short value on the right.
static void draw_check(spark_row_t* row, const char* text, const char* trailing, bool done) {
    style_row(row, SPARK_CARD_PADDING, 18, 16, 14);
    int32_t width = lv_obj_get_content_width(row->root);
    lv_obj_remove_flag(row->mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(row->mark, done ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    int32_t meta_width = spark_text_empty(trailing)
        ? 0 : LV_MIN(spark_text_width(row->meta, trailing), width / 3);
    spark_line(row->primary, text, 26, 0, width - 26 - (meta_width == 0 ? 0 : meta_width + 10));
    spark_line(row->meta, trailing, width - meta_width, 2, meta_width);
    hide(row->secondary);
    hide(row->address);
    hide(row->subject);
}

// A title, then a small lead and a preview on the second line.
static void draw_stacked(spark_row_t* row, const char* title, const char* lead, const char* preview) {
    style_row(row, 4, 16, 14, 12);
    int32_t width = lv_obj_get_content_width(row->root);
    lv_obj_add_flag(row->mark, LV_OBJ_FLAG_HIDDEN);
    int32_t lead_width = spark_text_empty(lead)
        ? 0 : LV_MIN(spark_text_width(row->meta, lead), width / 3);
    int32_t preview_x = lead_width == 0 ? 0 : lead_width + 10;
    spark_line(row->primary, title, 0, 0, width);
    spark_line(row->meta, lead, 0, 20, lead_width);
    spark_line(row->secondary, preview, preview_x, 20, width - preview_x);
    hide(row->address);
    hide(row->subject);
}

// A name, a note after it, and a value on the right; then an emphasised
// lead and the rest on the second line.
static void draw_mail(spark_row_t* row, const char* name, const char* note, const char* trailing,
                      const char* lead, const char* body) {
    style_row(row, 6, 18, 16, 14);
    int32_t width = lv_obj_get_content_width(row->root);
    lv_obj_add_flag(row->mark, LV_OBJ_FLAG_HIDDEN);
    int32_t meta_width = spark_text_empty(trailing)
        ? 0 : LV_MIN(spark_text_width(row->meta, trailing), width / 3);
    int32_t line_width = width - (meta_width == 0 ? 0 : meta_width + 10);
    int32_t name_width = LV_MIN(line_width, spark_text_width(row->primary, name));
    spark_line(row->primary, name, 0, 0, name_width);
    spark_line(row->address, note, name_width + 8, 2, line_width - name_width - 8);
    spark_line(row->meta, trailing, width - meta_width, 2, meta_width);
    int32_t lead_width = spark_text_empty(lead)
        ? 0 : LV_MIN(width, spark_text_width(row->subject, lead));
    int32_t body_x = lead_width == 0 ? 0 : lead_width + 8;
    spark_line(row->subject, lead, 0, SPARK_LINE_HEIGHT, lead_width);
    spark_line(row->secondary, body, body_x, SPARK_LINE_HEIGHT, width - body_x);
}

// A title, then one line under it.
static void draw_two_lines(spark_row_t* row, const char* title, const char* body) {
    style_row(row, 6, 18, 16, 14);
    int32_t width = lv_obj_get_content_width(row->root);
    lv_obj_add_flag(row->mark, LV_OBJ_FLAG_HIDDEN);
    spark_line(row->primary, title, 0, 0, width);
    spark_line(row->secondary, body, 0, SPARK_LINE_HEIGHT, width);
    hide(row->meta);
    hide(row->address);
    hide(row->subject);
}

static void draw_card(spark_row_t* row, const spark_display_card_t* card) {
    if (!card->is_grid) {
        draw_stacked(row, card->doc.text[0], NULL, card->doc.count > 1 ? card->doc.text[1] : NULL);
        return;
    }
    char* headings = spark_joined((const char* const*)card->grid.headers, card->grid.column_count, " · ");
    char* first = spark_joined((const char* const*)card->grid.cells[0], card->grid.column_count, " · ");
    draw_stacked(row, headings != NULL ? headings : first, NULL, headings != NULL ? first : NULL);
    free(headings);
    free(first);
}

static void render_item(spark_row_t* row, const spark_display_item_t* item, int32_t y, bool selected) {
    lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(row->root, LV_PCT(100), spark_item_row_height(item->type));
    lv_obj_set_y(row->root, y);
    switch (item->type) {
    case SPARK_ITEM_TODO:
        draw_check(row, spark_item_text(item, "content"), spark_item_text(item, "due"), item_done(item));
        break;
    case SPARK_ITEM_NOTE:
        draw_stacked(row, spark_item_text(item, "title"), spark_item_text(item, "date"),
                     spark_item_text(item, "content"));
        break;
    case SPARK_ITEM_EMAIL:
        draw_mail(row, spark_item_text(item, "sender"), spark_item_text(item, "address"),
                  spark_item_text(item, "time"), spark_item_text(item, "subject"),
                  spark_item_text(item, "preview"));
        break;
    case SPARK_ITEM_EMAIL_DRAFT:
        draw_mail(row, spark_item_text(item, "to"), NULL, spark_item_text(item, "status"),
                  spark_item_text(item, "subject"), spark_item_text(item, "body"));
        break;
    case SPARK_ITEM_CALENDAR_EVENT:
        draw_two_lines(row, spark_item_text(item, "title"), spark_item_text(item, "when"));
        break;
    case SPARK_ITEM_CONTACT: {
        const char* work[] = {spark_item_text(item, "jobTitle"), spark_item_text(item, "organization")};
        char* detail = spark_joined(work, 2, " · ");
        draw_stacked(row, spark_item_text(item, "name"), NULL,
                     detail != NULL ? detail : spark_item_text(item, "phone"));
        free(detail);
        break;
    }
    case SPARK_ITEM_PLACES:
        draw_stacked(row, spark_item_text(item, "name"), spark_item_text(item, "open"),
                     spark_item_text(item, "address"));
        break;
    case SPARK_ITEM_ROUTE: {
        const char* way[] = {spark_item_text(item, "distance"), spark_item_text(item, "via")};
        char* detail = spark_joined(way, 2, " · ");
        draw_stacked(row, spark_item_text(item, "title"), spark_item_text(item, "duration"), detail);
        free(detail);
        break;
    }
    case SPARK_ITEM_CARD:
        draw_card(row, item->card);
        break;
    case SPARK_ITEM_TYPE_COUNT:
        break;
    }
    select_row(row, selected, item_done(item));
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
        render_item(&s_rows[i - start], &display->items[i], y, i == display->selected);
        y += spark_item_row_height(display->items[i].type) + SPARK_DISPLAY_ROW_GAP;
    }
}

static void destroy(void) {
    memset(s_rows, 0, sizeof(s_rows));
}

void spark_body_list_select(size_t page_row, const spark_display_item_t* item, bool selected) {
    select_row(&s_rows[page_row], selected, item_done(item));
}

const spark_body_t spark_body_list = {create, reset, render, destroy};
