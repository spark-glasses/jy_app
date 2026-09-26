#include "view_internal.h"

#include <stdlib.h>
#include <string.h>

void spark_prepare_static_obj(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE |
                                LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
}

lv_obj_t* spark_wrap_label(lv_obj_t* parent, unsigned size) {
    lv_obj_t* label = lv_label_create(parent);
    if (label == NULL) return NULL;
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label, SPARK_TEXT_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, spark_font(size), LV_PART_MAIN);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
}

int32_t spark_text_width(lv_obj_t* label, const char* text) {
    lv_point_t size;
    lv_text_get_size(&size, spark_text(text), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int32_t spark_text_height(lv_obj_t* label, const char* text, int32_t width) {
    int32_t inset_h = lv_obj_get_style_pad_left(label, LV_PART_MAIN) +
                      lv_obj_get_style_pad_right(label, LV_PART_MAIN) +
                      2 * lv_obj_get_style_border_width(label, LV_PART_MAIN);
    int32_t inset_v = lv_obj_get_style_pad_top(label, LV_PART_MAIN) +
                      lv_obj_get_style_pad_bottom(label, LV_PART_MAIN) +
                      2 * lv_obj_get_style_border_width(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, spark_text(text), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     LV_MAX(1, width - inset_h), LV_TEXT_FLAG_NONE);
    return size.y + inset_v;
}

int32_t spark_place(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width) {
    lv_label_set_text_static(label, spark_text(text));
    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    return spark_text_height(label, text, width);
}

void spark_detach(lv_obj_t* label) {
    if (label == NULL) return;
    lv_label_set_text_static(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

char* spark_joined(const char* const* parts, size_t count, const char* separator) {
    size_t size = 1;
    for (size_t i = 0; i < count; ++i)
        if (!spark_text_empty(parts[i])) size += strlen(parts[i]) + strlen(separator);
    char* text = malloc(size);
    if (text == NULL) return NULL;
    text[0] = '\0';
    for (size_t i = 0; i < count; ++i) {
        if (spark_text_empty(parts[i])) continue;
        if (text[0] != '\0') strcat(text, separator);
        strcat(text, parts[i]);
    }
    if (text[0] == '\0') {
        free(text);
        return NULL;
    }
    return text;
}

void spark_line(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width) {
    // One line: line breaks in the text read as spaces. LVGL cuts the text as
    // it is set, so they are replaced first.
    const char* value = spark_text(text);
    char* flat = NULL;
    if (strpbrk(value, "\n\r\t") != NULL && (flat = malloc(strlen(value) + 1)) != NULL) {
        strcpy(flat, value);
        for (char* c = flat; *c != '\0'; ++c)
            if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
    }
    // DOT mode can modify its buffer. Give LVGL an owned copy, never a model string.
    lv_label_set_text(label, flat != NULL ? flat : value);
    free(flat);
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, LV_MAX(1, width), SPARK_LINE_HEIGHT);
    lv_obj_update_flag(label, LV_OBJ_FLAG_HIDDEN, width <= 0 || spark_text_empty(text));
}
