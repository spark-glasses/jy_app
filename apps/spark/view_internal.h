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

// A list, an item's detail page, or a card's grid or text blocks.
typedef enum {
    SPARK_BODY_LIST, SPARK_BODY_DETAIL, SPARK_BODY_GRID, SPARK_BODY_DOC
} spark_body_kind_t;

#define SPARK_BODY_COUNT 4

extern const spark_body_t spark_body_list;
extern const spark_body_t spark_body_detail;
extern const spark_body_t spark_body_grid;
extern const spark_body_t spark_body_doc;

void spark_body_list_select(size_t page_row, const spark_display_item_t* item, bool selected);
// The header of an item's detail page; both empty for a card, which has none.
void spark_item_heading(const spark_display_item_t* item, const char** title, const char** hint);

void spark_prepare_static_obj(lv_obj_t* obj);
lv_obj_t* spark_wrap_label(lv_obj_t* parent, unsigned size);
int32_t spark_text_width(lv_obj_t* label, const char* text);
int32_t spark_text_height(lv_obj_t* label, const char* text, int32_t width);
void spark_line(lv_obj_t* label, const char* text, int32_t x, int32_t y, int32_t width);
// The non-empty parts joined with the separator, in a new buffer; NULL when all are empty.
char* spark_joined(const char* const* parts, size_t count, const char* separator);
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
// A device-side clear that keeps the display identity and reports `dismissed`.
void spark_navigation_dismiss(void);

uint64_t spark_monotonic_ms(void);
void spark_host_connection_changed(bool connected);
bool spark_reply_visible(void);
