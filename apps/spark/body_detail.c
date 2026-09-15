#include "view_internal.h"

#define SPARK_DETAIL_TOP 8
#define SPARK_DETAIL_GAP 12

static lv_obj_t* s_lead;
static lv_obj_t* s_body;
static lv_obj_t* s_meta;

static lv_obj_t* label(lv_obj_t* content, unsigned size) {
    lv_obj_t* obj = lv_label_create(content);
    if (obj == NULL) return NULL;
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_font(obj, spark_font(size), LV_PART_MAIN);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

static bool create(lv_obj_t* content) {
    s_lead = label(content, 18);
    s_body = label(content, SPARK_TEXT_FONT_SIZE);
    s_meta = label(content, 14);
    if (s_lead == NULL || s_body == NULL || s_meta == NULL) return false;
    lv_obj_set_y(s_lead, SPARK_DETAIL_TOP);
    lv_obj_set_style_text_line_space(s_body, SPARK_TEXT_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_meta, lv_color_make(170, 170, 170), LV_PART_MAIN);
    return true;
}

static void reset(void) {
    lv_obj_t* labels[] = {s_lead, s_body, s_meta};
    for (size_t i = 0; i < 3; ++i) {
        lv_label_set_text_static(labels[i], "");
        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void render(const spark_display_t* display) {
    const spark_display_row_t* row = &display->rows[0];
    lv_obj_t* parent = lv_obj_get_parent(s_lead);
    lv_obj_update_layout(parent);
    int32_t width = lv_obj_get_content_width(parent);
    lv_label_set_text_static(s_lead, spark_text(row->primary));
    lv_label_set_text_static(s_body, spark_text(row->secondary));
    lv_label_set_text_static(s_meta, spark_text(row->meta));
    lv_obj_remove_flag(s_lead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    int32_t body_y = spark_text_empty(row->primary) ? SPARK_DETAIL_TOP :
        SPARK_DETAIL_TOP + spark_text_height(s_lead, row->primary, width) + SPARK_DETAIL_GAP;
    lv_obj_set_y(s_body, body_y);
    lv_obj_set_y(s_meta, body_y + spark_text_height(s_body, row->secondary, width) + SPARK_DETAIL_GAP);
}

static void destroy(void) {
    s_lead = s_body = s_meta = NULL;
}

const spark_body_t spark_body_detail = {create, reset, render, destroy};
