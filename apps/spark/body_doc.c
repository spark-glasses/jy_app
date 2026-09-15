#include "view_internal.h"

#include <string.h>

#define SPARK_DOC_TOP 8
#define SPARK_DOC_BLOCK_GAP 6
#define SPARK_DOC_HEADING_GAP 10
#define SPARK_DOC_HEADING_FONT 16

static lv_obj_t* s_doc;
static lv_obj_t* s_blocks[SPARK_DISPLAY_MAX_BLOCKS];

static lv_obj_t* block(size_t index) {
    if (s_blocks[index] == NULL) s_blocks[index] = spark_wrap_label(s_doc, SPARK_TEXT_FONT_SIZE);
    return s_blocks[index];
}

static bool create(lv_obj_t* content) {
    s_doc = lv_obj_create(content);
    if (s_doc == NULL) return false;
    spark_prepare_static_obj(s_doc);
    lv_obj_add_flag(s_doc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_doc, 0, 0);
    lv_obj_set_size(s_doc, LV_PCT(100), 1);
    return true;
}

static void reset(void) {
    for (size_t i = 0; i < SPARK_DISPLAY_MAX_BLOCKS; ++i) spark_detach(s_blocks[i]);
    lv_obj_add_flag(s_doc, LV_OBJ_FLAG_HIDDEN);
}

static void render(const spark_display_t* display) {
    const spark_display_doc_t* doc = &display->body.doc;
    lv_obj_t* content = lv_obj_get_parent(s_doc);
    lv_obj_remove_flag(s_doc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(content);
    int32_t width = lv_obj_get_content_width(content);
    int32_t y = SPARK_DOC_TOP;
    for (size_t i = 0; i < doc->count; ++i) {
        lv_obj_t* label = block(i);
        if (label == NULL) return;
        bool heading = doc->heading[i];
        if (heading && i > 0) y += SPARK_DOC_HEADING_GAP - SPARK_DOC_BLOCK_GAP;
        lv_obj_set_style_text_font(
            label, spark_font(heading ? SPARK_DOC_HEADING_FONT : SPARK_TEXT_FONT_SIZE), LV_PART_MAIN);
        y += spark_place(label, doc->text[i], 0, y, width) + SPARK_DOC_BLOCK_GAP;
    }
    lv_obj_set_size(s_doc, width, y);
}

static void destroy(void) {
    s_doc = NULL;
    memset(s_blocks, 0, sizeof(s_blocks));
}

const spark_body_t spark_body_doc = {create, reset, render, destroy};
