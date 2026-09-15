#pragma once

#include "display.h"
#include "fonts.h"
#include "lvgl/lvgl.h"

#define SPARK_LINE_HEIGHT 24
#define SPARK_CARD_PADDING 8

typedef struct {
    bool (*create)(lv_obj_t* content);
    void (*reset)(void);
    void (*render)(const spark_display_t* display);
    void (*destroy)(void);
} spark_body_t;

extern const spark_body_t spark_body_list;
extern const spark_body_t spark_body_detail;
extern const spark_body_t spark_body_grid;
extern const spark_body_t spark_body_doc;

void spark_body_list_select(size_t page_row, const spark_display_row_t* row, bool selected);

void spark_prepare_static_obj(lv_obj_t* obj);
lv_obj_t* spark_wrap_label(lv_obj_t* parent, unsigned size);
int32_t spark_text_width(lv_obj_t* label, const char* text);
int32_t spark_text_height(lv_obj_t* label, const char* text, int32_t width);
void spark_line(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width);
int32_t spark_place(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width);
void spark_detach(lv_obj_t* label);

void spark_view_render(const spark_display_t* display);
void spark_view_scroll(bool forward);

bool spark_reply_create(lv_obj_t* footer);
void spark_reply_destroy(void);

void spark_navigation_move(bool forward);
void spark_navigation_open(void);
void spark_navigation_back(void);
void spark_navigation_clear(void);
