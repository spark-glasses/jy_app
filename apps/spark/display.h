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
#define SPARK_ITEM_MAX_FIELDS 6

// One per artifact type the phone sends. Each type has its own fields, list
// card, and detail page.
typedef enum {
    SPARK_ITEM_TODO,
    SPARK_ITEM_NOTE,
    SPARK_ITEM_EMAIL,
    SPARK_ITEM_EMAIL_DRAFT,
    SPARK_ITEM_CALENDAR_EVENT,
    SPARK_ITEM_CONTACT,
    SPARK_ITEM_PLACES,
    SPARK_ITEM_ROUTE,
    SPARK_ITEM_CARD,
    SPARK_ITEM_TYPE_COUNT
} spark_item_type_t;

typedef enum { SPARK_DISPLAY_LIST, SPARK_DISPLAY_ITEM } spark_display_kind_t;

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

// A card is text blocks or a grid of columns.
typedef struct {
    bool is_grid;
    union {
        spark_display_doc_t doc;
        spark_display_grid_t grid;
    };
} spark_display_card_t;

typedef struct {
    char* id;
    spark_item_type_t type;
    bool has_more;
    // In the order of the type's fields; absent optional text is NULL.
    char* text[SPARK_ITEM_MAX_FIELDS];
    bool flag[SPARK_ITEM_MAX_FIELDS];
    spark_display_card_t* card;
} spark_display_item_t;

typedef struct {
    spark_display_kind_t kind;
    // Lists only.
    char* title;
    char* hint;
    size_t selected;
    size_t count;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t page_starts[SPARK_DISPLAY_MAX_ITEMS];
    spark_display_item_t items[SPARK_DISPLAY_MAX_ITEMS];
} spark_display_t;

static inline const char* spark_text(const char* text) { return text == NULL ? "" : text; }
static inline bool spark_text_empty(const char* text) { return text == NULL || text[0] == '\0'; }

// Owns copied strings; the message tree may be destroyed after parsing.
spark_display_t* spark_display_parse(mpack_node_t page);
spark_display_t* spark_display_parse_item(mpack_node_t item);
void spark_display_free(spark_display_t* display);
bool spark_display_read_revision(mpack_node_t data, uint64_t* revision);
bool spark_display_read_counter(mpack_node_t data, const char* key, uint64_t* value);

// A text field of the item's type, or NULL when it is absent.
const char* spark_item_text(const spark_display_item_t* item, const char* key);
bool spark_item_flag(const spark_display_item_t* item, const char* key);
// The height of the item's list card; lists are paged with these heights.
uint32_t spark_item_row_height(spark_item_type_t type);
bool spark_item_equal(const spark_display_item_t* a, const spark_display_item_t* b);
