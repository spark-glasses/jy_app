#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mpack.h>

#define SPARK_DISPLAY_MAX_ITEMS 20
#define SPARK_DISPLAY_VISIBLE_ROWS 5
#define SPARK_DISPLAY_MAX_REQUEST_BYTES 49152
#define SPARK_DISPLAY_MAX_LIST_TEXT 256
#define SPARK_DISPLAY_MAX_TEXT 4096
#define SPARK_DISPLAY_MAX_REPLY (UINT16_MAX - 1)
#define SPARK_DISPLAY_CONTENT_HEIGHT 256
#define SPARK_DISPLAY_ROW_GAP 4

typedef enum {
    SPARK_LAYOUT_DETAIL, SPARK_LAYOUT_REMINDER, SPARK_LAYOUT_NOTE,
    SPARK_LAYOUT_EMAIL, SPARK_LAYOUT_EVENT
} spark_row_layout_t;

typedef struct {
    char* id;
    char* mark;
    char* primary;
    char* secondary;
    char* meta;
    char* address;
    char* subject;
    spark_row_layout_t layout;
    uint32_t height;
} spark_display_row_t;

typedef struct {
    bool is_list;
    char* title;
    char* hint;
    size_t selected;
    size_t count;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t row_gap;
    uint32_t page_starts[SPARK_DISPLAY_MAX_ITEMS];
    spark_display_row_t rows[SPARK_DISPLAY_MAX_ITEMS];
} spark_display_t;

// Owns copied strings; the message tree may be destroyed after parsing.
spark_display_t* spark_display_parse(mpack_node_t page);
spark_display_t* spark_display_parse_item(mpack_node_t item);
void spark_display_free(spark_display_t* display);
bool spark_display_read_revision(mpack_node_t data, uint64_t* revision);
bool spark_display_read_counter(mpack_node_t data, const char* key, uint64_t* value);
