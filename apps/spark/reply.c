#include "spark.h"
#include "view_internal.h"

#define SPARK_REPLY_LEFT 76
#define SPARK_REPLY_RIGHT 8
#define SPARK_REPLY_HEIGHT 68
#define SPARK_REPLY_BOTTOM 8
#define SPARK_REPLY_PADDING_H 8
#define SPARK_REPLY_PADDING_V 6
#define SPARK_REPLY_BORDER_WIDTH 0
#define SPARK_REPLY_MAX_LINES 3

static lv_obj_t* s_footer;
static lv_obj_t* s_panel;
static lv_obj_t* s_label;

bool spark_reply_create(lv_obj_t* footer) {
    s_footer = footer;
    s_panel = lv_obj_create(footer);
    s_label = s_panel != NULL ? lv_label_create(s_panel) : NULL;
    if (s_panel == NULL || s_label == NULL) return false;
    lv_obj_set_size(s_panel, 1, SPARK_REPLY_HEIGHT);
    lv_obj_align(s_panel, LV_ALIGN_BOTTOM_LEFT, SPARK_REPLY_LEFT, -SPARK_REPLY_BOTTOM);
    lv_obj_set_style_bg_color(s_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_panel, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, SPARK_REPLY_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_panel, SPARK_REPLY_PADDING_H, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_panel, SPARK_REPLY_PADDING_V, LV_PART_MAIN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                    LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_line_space(s_label, SPARK_TEXT_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_size(s_label, 1, 1);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_label, spark_font(SPARK_TEXT_FONT_SIZE), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_text(s_label, "");
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void spark_reply_destroy(void) {
    s_footer = s_panel = s_label = NULL;
}

bool spark_reply_set(const char* text) {
    if (!spark_display_ready() || s_panel == NULL || s_label == NULL ||
        !lv_obj_is_valid(s_panel) || !lv_obj_is_valid(s_label)) {
        return false;
    }

    if (spark_text_empty(text)) {
        lv_label_set_text(s_label, "");
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        return true;
    }

    lv_label_set_text(s_label, text);
    const lv_font_t* font = lv_obj_get_style_text_font(s_label, LV_PART_MAIN);
    lv_coord_t max_panel_width = LV_MAX(
        1, lv_obj_get_content_width(s_footer) - SPARK_REPLY_LEFT - SPARK_REPLY_RIGHT);
    lv_coord_t insets = 2 * (SPARK_REPLY_PADDING_H + SPARK_REPLY_BORDER_WIDTH);
    lv_coord_t max_text_width = LV_MAX(1, max_panel_width - insets);
    lv_point_t size = {0, 0};
    lv_text_get_size(
        &size, text, font, 0, SPARK_TEXT_LINE_SPACE,
        max_text_width, LV_TEXT_FLAG_NONE);
    lv_coord_t panel_width = LV_MIN(max_panel_width, LV_MAX(1, size.x) + insets);
    lv_obj_set_width(s_panel, panel_width);
    lv_obj_set_height(s_panel, SPARK_REPLY_HEIGHT);
    lv_obj_update_layout(s_panel);
    lv_coord_t label_width = LV_MAX(1, lv_obj_get_content_width(s_panel));
    lv_coord_t content_height = LV_MAX(1, lv_obj_get_content_height(s_panel));
    lv_coord_t max_lines_height =
        (lv_coord_t)lv_font_get_line_height(font) * SPARK_REPLY_MAX_LINES +
        SPARK_TEXT_LINE_SPACE * (SPARK_REPLY_MAX_LINES - 1);
    lv_text_get_size(
        &size, text, font, 0, SPARK_TEXT_LINE_SPACE,
        label_width, LV_TEXT_FLAG_NONE);
    lv_obj_set_size(
        s_label,
        label_width,
        LV_MIN(content_height, LV_MIN(max_lines_height, LV_MAX(1, size.y))));
    lv_obj_align(s_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    return true;
}
