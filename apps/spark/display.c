#include "display.h"

#include <stdlib.h>
#include <string.h>

typedef enum { SPARK_FIELD_TEXT, SPARK_FIELD_FLAG } spark_field_kind_t;

typedef struct {
    const char* key;
    spark_field_kind_t kind;
    bool required;
} spark_field_t;

typedef struct {
    const char* name;
    uint32_t row_height;
    size_t field_count;
    spark_field_t fields[SPARK_ITEM_MAX_FIELDS];
} spark_item_spec_t;

#define TEXT(key) {key, SPARK_FIELD_TEXT, true}
#define OPTIONAL(key) {key, SPARK_FIELD_TEXT, false}
#define FLAG(key) {key, SPARK_FIELD_FLAG, true}

// The fields each type carries, exactly as the phone sends them. A card's
// blocks or grid are parsed separately.
static const spark_item_spec_t s_specs[SPARK_ITEM_TYPE_COUNT] = {
    [SPARK_ITEM_TODO] = {"todo", 40, 4,
        {TEXT("content"), FLAG("completed"), OPTIONAL("due"), OPTIONAL("repeat")}},
    [SPARK_ITEM_NOTE] = {"note", 48, 3, {TEXT("title"), TEXT("content"), OPTIONAL("date")}},
    [SPARK_ITEM_EMAIL] = {"email", 60, 6,
        {TEXT("sender"), OPTIONAL("address"), TEXT("subject"), TEXT("preview"), TEXT("time"),
         TEXT("sentAt")}},
    [SPARK_ITEM_EMAIL_DRAFT] = {"email_draft", 60, 5,
        {TEXT("to"), TEXT("from"), TEXT("subject"), TEXT("body"), TEXT("status")}},
    [SPARK_ITEM_CALENDAR_EVENT] = {"calendar_event", 60, 4,
        {TEXT("title"), TEXT("when"), OPTIONAL("location"), OPTIONAL("response")}},
    [SPARK_ITEM_CONTACT] = {"contact", 48, 5,
        {TEXT("name"), OPTIONAL("organization"), OPTIONAL("jobTitle"), OPTIONAL("phone"),
         OPTIONAL("email")}},
    [SPARK_ITEM_PLACES] = {"places", 48, 4,
        {TEXT("name"), OPTIONAL("address"), OPTIONAL("rating"), OPTIONAL("open")}},
    [SPARK_ITEM_ROUTE] = {"route", 48, 5,
        {TEXT("title"), OPTIONAL("via"), OPTIONAL("duration"), OPTIONAL("distance"),
         OPTIONAL("mode")}},
    [SPARK_ITEM_CARD] = {"card", 48, 0, {{0}}},
};

static bool node_is(mpack_node_t node, const char* text) {
    return mpack_node_type(node) == mpack_type_str && mpack_node_strlen(node) == strlen(text) &&
           memcmp(mpack_node_str(node), text, strlen(text)) == 0;
}

bool spark_display_read_counter(mpack_node_t data, const char* key, uint64_t* revision) {
    mpack_node_t node = mpack_node_map_cstr(data, key);
    if (mpack_node_type(node) != mpack_type_str) return false;
    size_t length = mpack_node_strlen(node);
    const char* value = mpack_node_str(node);
    if (length == 0 || length > 20 || (length > 1 && value[0] == '0')) return false;
    uint64_t number = 0;
    for (size_t i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        unsigned digit = (unsigned)(value[i] - '0');
        if (number > (UINT64_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    *revision = number;
    return true;
}

bool spark_display_read_revision(mpack_node_t data, uint64_t* revision) {
    return spark_display_read_counter(data, "revision", revision);
}

static void free_card(spark_display_card_t* card) {
    if (card == NULL) return;
    if (card->is_grid) {
        for (size_t c = 0; c < card->grid.column_count; ++c) {
            free(card->grid.headers[c]);
            for (size_t r = 0; r < card->grid.row_count; ++r) free(card->grid.cells[r][c]);
        }
    } else {
        for (size_t i = 0; i < card->doc.count; ++i) free(card->doc.text[i]);
    }
    free(card);
}

// `[[tag, text], ...]`: one through 24 blocks, tag "h" or "p", text not empty.
static bool parse_blocks(mpack_node_t blocks, spark_display_doc_t* doc) {
    if (mpack_node_type(blocks) != mpack_type_array) return false;
    size_t count = mpack_node_array_length(blocks);
    if (count == 0 || count > SPARK_DISPLAY_MAX_BLOCKS) return false;
    doc->count = count;
    for (size_t i = 0; i < count; ++i) {
        mpack_node_t block = mpack_node_array_at(blocks, i);
        if (mpack_node_type(block) != mpack_type_array || mpack_node_array_length(block) != 2)
            return false;
        mpack_node_t tag = mpack_node_array_at(block, 0);
        if (node_is(tag, "h")) doc->heading[i] = true;
        else if (!node_is(tag, "p")) return false;
        doc->text[i] = mpack_node_utf8_cstr_alloc(
            mpack_node_array_at(block, 1), SPARK_DISPLAY_MAX_TEXT + 1);
        if (spark_text_empty(doc->text[i])) return false;
    }
    return true;
}

// Two or three column headings (empty strings allowed) and one through twenty
// rows with exactly one cell per column.
static bool parse_grid(mpack_node_t headers, mpack_node_t rows, spark_display_grid_t* grid) {
    if (mpack_node_type(headers) != mpack_type_array || mpack_node_type(rows) != mpack_type_array)
        return false;
    size_t columns = mpack_node_array_length(headers);
    size_t row_count = mpack_node_array_length(rows);
    if (columns < SPARK_DISPLAY_MIN_COLUMNS || columns > SPARK_DISPLAY_MAX_COLUMNS ||
        row_count == 0 || row_count > SPARK_DISPLAY_MAX_GRID_ROWS)
        return false;
    grid->column_count = columns;
    grid->row_count = row_count;
    for (size_t c = 0; c < columns; ++c) {
        grid->headers[c] = mpack_node_utf8_cstr_alloc(
            mpack_node_array_at(headers, c), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
        if (grid->headers[c] == NULL) return false;
    }
    for (size_t r = 0; r < row_count; ++r) {
        mpack_node_t row = mpack_node_array_at(rows, r);
        if (mpack_node_type(row) != mpack_type_array || mpack_node_array_length(row) != columns)
            return false;
        for (size_t c = 0; c < columns; ++c) {
            grid->cells[r][c] = mpack_node_utf8_cstr_alloc(
                mpack_node_array_at(row, c), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
            if (grid->cells[r][c] == NULL) return false;
        }
    }
    return true;
}

// A card carries `blocks`, or `headers` and `rows`, never both.
static spark_display_card_t* parse_card(mpack_node_t node, size_t* keys) {
    bool has_blocks = mpack_node_map_contains_cstr(node, "blocks");
    bool has_headers = mpack_node_map_contains_cstr(node, "headers");
    bool has_rows = mpack_node_map_contains_cstr(node, "rows");
    if (has_headers != has_rows || has_blocks == has_headers) return NULL;
    spark_display_card_t* card = calloc(1, sizeof(*card));
    if (card == NULL) return NULL;
    card->is_grid = has_headers;
    bool parsed = has_blocks
        ? parse_blocks(mpack_node_map_cstr(node, "blocks"), &card->doc)
        : parse_grid(mpack_node_map_cstr(node, "headers"), mpack_node_map_cstr(node, "rows"),
                     &card->grid);
    if (!parsed) {
        free_card(card);
        return NULL;
    }
    *keys += has_blocks ? 1 : 2;
    return card;
}

// `{id, type, hasMore, ...fields}`. Every key belongs to the type, and every
// required field is present with its kind of value.
static bool parse_item(mpack_node_t node, spark_display_item_t* item) {
    if (mpack_node_type(node) != mpack_type_map) return false;
    mpack_node_t type = mpack_node_map_cstr(node, "type");
    const spark_item_spec_t* spec = NULL;
    for (size_t t = 0; t < SPARK_ITEM_TYPE_COUNT && spec == NULL; ++t) {
        if (node_is(type, s_specs[t].name)) {
            spec = &s_specs[t];
            item->type = (spark_item_type_t)t;
        }
    }
    if (spec == NULL) return false;
    item->id = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "id"), 257);
    mpack_node_t has_more = mpack_node_map_cstr(node, "hasMore");
    if (spark_text_empty(item->id) || mpack_node_type(has_more) != mpack_type_bool) return false;
    item->has_more = mpack_node_bool(has_more);
    size_t keys = 3;
    for (size_t f = 0; f < spec->field_count; ++f) {
        const spark_field_t* field = &spec->fields[f];
        if (!mpack_node_map_contains_cstr(node, field->key)) {
            if (field->required) return false;
            continue;
        }
        ++keys;
        mpack_node_t value = mpack_node_map_cstr(node, field->key);
        if (field->kind == SPARK_FIELD_FLAG) {
            if (mpack_node_type(value) != mpack_type_bool) return false;
            item->flag[f] = mpack_node_bool(value);
        } else {
            item->text[f] = mpack_node_utf8_cstr_alloc(value, SPARK_DISPLAY_MAX_TEXT + 1);
            if (item->text[f] == NULL) return false;
        }
    }
    if (item->type == SPARK_ITEM_CARD) {
        item->card = parse_card(node, &keys);
        if (item->card == NULL) return false;
    }
    return mpack_node_map_count(node) == keys && mpack_node_error(node) == mpack_ok;
}

// `{title, hint, selected, items}`. Pages are packed as the phone packs them:
// at most five cards, and cards plus gaps within the content height.
spark_display_t* spark_display_parse(mpack_node_t page) {
    if (mpack_node_type(page) != mpack_type_map || mpack_node_map_count(page) != 4) return NULL;
    mpack_node_t items = mpack_node_map_cstr(page, "items");
    if (mpack_node_type(items) != mpack_type_array) return NULL;
    size_t count = mpack_node_array_length(items);
    if (count > SPARK_DISPLAY_MAX_ITEMS) return NULL;
    uint32_t selected = mpack_node_u32(mpack_node_map_cstr(page, "selected"));
    if ((count == 0 && selected != 0) || (count > 0 && selected >= count)) return NULL;
    spark_display_t* display = calloc(1, sizeof(*display));
    if (display == NULL) return NULL;
    display->kind = SPARK_DISPLAY_LIST;
    display->count = count;
    display->selected = selected;
    display->title = mpack_node_utf8_cstr_alloc(
        mpack_node_map_cstr(page, "title"), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
    display->hint = mpack_node_utf8_cstr_alloc(
        mpack_node_map_cstr(page, "hint"), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
    if (display->title == NULL || display->hint == NULL) goto invalid;
    if (count != 0) display->page_count = 1;
    uint32_t used = 0;
    size_t page_items = 0;
    for (size_t i = 0; i < count; ++i) {
        spark_display_item_t* item = &display->items[i];
        if (!parse_item(mpack_node_array_at(items, i), item)) goto invalid;
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(item->id, display->items[j].id) == 0) goto invalid;
        }
        uint32_t height = s_specs[item->type].row_height;
        if (page_items && (page_items == SPARK_DISPLAY_VISIBLE_ROWS ||
                           used + SPARK_DISPLAY_ROW_GAP + height > SPARK_DISPLAY_CONTENT_HEIGHT)) {
            display->page_starts[display->page_count++] = i;
            used = 0;
            page_items = 0;
        }
        used += height + (page_items == 0 ? 0 : SPARK_DISPLAY_ROW_GAP);
        ++page_items;
        if (i == selected) display->page_index = display->page_count;
    }
    if (mpack_node_error(page) != mpack_ok) goto invalid;
    return display;
invalid:
    spark_display_free(display);
    return NULL;
}

spark_display_t* spark_display_parse_item(mpack_node_t node) {
    spark_display_t* display = calloc(1, sizeof(*display));
    if (display == NULL) return NULL;
    display->kind = SPARK_DISPLAY_ITEM;
    display->count = 1;
    if (!parse_item(node, &display->items[0])) {
        spark_display_free(display);
        return NULL;
    }
    return display;
}

void spark_display_free(spark_display_t* display) {
    if (display == NULL) return;
    free(display->title);
    free(display->hint);
    for (size_t i = 0; i < display->count; ++i) {
        spark_display_item_t* item = &display->items[i];
        free(item->id);
        for (size_t f = 0; f < SPARK_ITEM_MAX_FIELDS; ++f) free(item->text[f]);
        free_card(item->card);
    }
    free(display);
}

static int field_index(const spark_display_item_t* item, const char* key, spark_field_kind_t kind) {
    const spark_item_spec_t* spec = &s_specs[item->type];
    for (size_t f = 0; f < spec->field_count; ++f) {
        if (spec->fields[f].kind == kind && strcmp(spec->fields[f].key, key) == 0) return (int)f;
    }
    return -1;
}

const char* spark_item_text(const spark_display_item_t* item, const char* key) {
    int f = field_index(item, key, SPARK_FIELD_TEXT);
    return f < 0 ? NULL : item->text[f];
}

bool spark_item_flag(const spark_display_item_t* item, const char* key) {
    int f = field_index(item, key, SPARK_FIELD_FLAG);
    return f >= 0 && item->flag[f];
}

uint32_t spark_item_row_height(spark_item_type_t type) {
    return s_specs[type].row_height;
}

static bool same_text(const char* a, const char* b) {
    return (a == NULL) == (b == NULL) && strcmp(spark_text(a), spark_text(b)) == 0;
}

static bool same_card(const spark_display_card_t* a, const spark_display_card_t* b) {
    if (a == NULL || b == NULL) return a == b;
    if (a->is_grid != b->is_grid) return false;
    if (!a->is_grid) {
        if (a->doc.count != b->doc.count) return false;
        for (size_t i = 0; i < a->doc.count; ++i) {
            if (a->doc.heading[i] != b->doc.heading[i] || !same_text(a->doc.text[i], b->doc.text[i]))
                return false;
        }
        return true;
    }
    if (a->grid.column_count != b->grid.column_count || a->grid.row_count != b->grid.row_count)
        return false;
    for (size_t c = 0; c < a->grid.column_count; ++c) {
        if (!same_text(a->grid.headers[c], b->grid.headers[c])) return false;
        for (size_t r = 0; r < a->grid.row_count; ++r) {
            if (!same_text(a->grid.cells[r][c], b->grid.cells[r][c])) return false;
        }
    }
    return true;
}

bool spark_item_equal(const spark_display_item_t* a, const spark_display_item_t* b) {
    if (a->type != b->type || a->has_more != b->has_more || !same_text(a->id, b->id)) return false;
    for (size_t f = 0; f < SPARK_ITEM_MAX_FIELDS; ++f) {
        if (a->flag[f] != b->flag[f] || !same_text(a->text[f], b->text[f])) return false;
    }
    return same_card(a->card, b->card);
}
