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
#define SPARK_DISPLAY_MIN_COLUMNS 2
#define SPARK_DISPLAY_MAX_COLUMNS 3
#define SPARK_DISPLAY_MAX_GRID_ROWS 20
#define SPARK_DISPLAY_MAX_BLOCKS 24

typedef enum {
    SPARK_LAYOUT_DETAIL, SPARK_LAYOUT_REMINDER, SPARK_LAYOUT_NOTE,
    SPARK_LAYOUT_EMAIL, SPARK_LAYOUT_EVENT
} spark_row_layout_t;

typedef enum {
    SPARK_DISPLAY_LIST, SPARK_DISPLAY_DETAIL, SPARK_DISPLAY_GRID, SPARK_DISPLAY_DOC
} spark_display_kind_t;

#define SPARK_DISPLAY_KIND_COUNT 4

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
    size_t column_count;
    size_t row_count;
    char* headers[SPARK_DISPLAY_MAX_COLUMNS];
    char* cells[SPARK_DISPLAY_MAX_GRID_ROWS][SPARK_DISPLAY_MAX_COLUMNS];
} spark_display_grid_t;

typedef struct {
    size_t count;
    bool heading[SPARK_DISPLAY_MAX_BLOCKS];
    char* text[SPARK_DISPLAY_MAX_BLOCKS];
} spark_display_doc_t;

typedef struct {
    spark_display_kind_t kind;
    char* title;
    char* hint;
    size_t selected;
    size_t count;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t row_gap;
    uint32_t page_starts[SPARK_DISPLAY_MAX_ITEMS];
    spark_display_row_t rows[SPARK_DISPLAY_MAX_ITEMS];
    union {
        spark_display_grid_t grid;
        spark_display_doc_t doc;
    } body;
} spark_display_t;

static inline const char* spark_text(const char* text) { return text == NULL ? "" : text; }
static inline bool spark_text_empty(const char* text) { return text == NULL || text[0] == '\0'; }

// Owns copied strings; the message tree may be destroyed after parsing.
spark_display_t* spark_display_parse(mpack_node_t page);
spark_display_t* spark_display_parse_item(mpack_node_t item);
spark_display_t* spark_display_parse_grid(mpack_node_t grid);
spark_display_t* spark_display_parse_doc(mpack_node_t doc);
void spark_display_free(spark_display_t* display);
bool spark_display_read_revision(mpack_node_t data, uint64_t* revision);
bool spark_display_read_counter(mpack_node_t data, const char* key, uint64_t* value);
