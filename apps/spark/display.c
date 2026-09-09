#include "display.h"

#include <stdlib.h>
#include <string.h>

static bool node_is(mpack_node_t node, const char* text) {
    return mpack_node_type(node) == mpack_type_str && mpack_node_strlen(node) == strlen(text) &&
           memcmp(mpack_node_str(node), text, strlen(text)) == 0;
}

static char* optional_text(mpack_node_t node, const char* key) {
    if (!mpack_node_map_contains_cstr(node, key)) return NULL;
    return mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, key), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
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

spark_display_t* spark_display_parse(mpack_node_t page) {
    if (mpack_node_type(page) != mpack_type_map) return NULL;
    mpack_node_t kind = mpack_node_map_cstr(page, "kind");
    if (mpack_node_type(kind) != mpack_type_str || mpack_node_strlen(kind) != 4) return NULL;
    bool is_list = memcmp(mpack_node_str(kind), "list", 4) == 0;
    if (!is_list && memcmp(mpack_node_str(kind), "item", 4) != 0) return NULL;
    mpack_node_t rows = mpack_node_map_cstr(page, "rows");
    if (mpack_node_type(rows) != mpack_type_array) return NULL;
    size_t count = mpack_node_array_length(rows);
    if (count > SPARK_DISPLAY_MAX_ITEMS || (!is_list && count != 1)) return NULL;
    uint32_t selected = mpack_node_u32(mpack_node_map_cstr(page, "selected"));
    if ((count == 0 && selected != 0) || (count > 0 && selected >= count)) return NULL;
    spark_display_t* display = calloc(1, sizeof(*display));
    if (display == NULL) return NULL;
    display->is_list = is_list;
    display->count = count;
    display->selected = selected;
    size_t text_limit = (is_list ? SPARK_DISPLAY_MAX_LIST_TEXT : SPARK_DISPLAY_MAX_TEXT) + 1;
    display->title = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(page, "title"), text_limit);
    display->hint = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(page, "hint"), text_limit);
    if (display->title == NULL || display->hint == NULL) goto invalid;
    if (is_list && count != 0) {
        display->page_count = 1;
        display->row_gap = mpack_node_u32(mpack_node_map_cstr(page, "rowGap"));
        if (display->row_gap != SPARK_DISPLAY_ROW_GAP) goto invalid;
    }
    uint32_t used = 0;
    size_t page_rows = 0;
    for (size_t i = 0; i < count; ++i) {
        mpack_node_t node = mpack_node_array_at(rows, i);
        spark_display_row_t* row = &display->rows[i];
        row->id = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "id"), 257);
        row->mark = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "mark"), text_limit);
        row->primary = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "primary"), text_limit);
        row->secondary = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "secondary"), text_limit);
        row->meta = mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, "meta"), text_limit);
        if (row->id == NULL || row->id[0] == '\0' || row->mark == NULL || row->primary == NULL ||
            row->secondary == NULL || row->meta == NULL) goto invalid;
        if (is_list) {
            mpack_node_t layout = mpack_node_map_cstr(node, "layout");
            if (node_is(layout, "reminder")) row->layout = SPARK_LAYOUT_REMINDER;
            else if (node_is(layout, "note")) row->layout = SPARK_LAYOUT_NOTE;
            else if (node_is(layout, "email")) row->layout = SPARK_LAYOUT_EMAIL;
            else if (node_is(layout, "event")) row->layout = SPARK_LAYOUT_EVENT;
            else goto invalid;
            row->height = mpack_node_u32(mpack_node_map_cstr(node, "height"));
            uint32_t expected = row->layout == SPARK_LAYOUT_REMINDER ? 40 :
                row->layout == SPARK_LAYOUT_EVENT && row->meta[0] != '\0' ? 88 : 64;
            if (row->height != expected) goto invalid;
            if (page_rows && (page_rows == SPARK_DISPLAY_VISIBLE_ROWS ||
                             used + display->row_gap + row->height > SPARK_DISPLAY_CONTENT_HEIGHT)) {
                display->page_starts[display->page_count++] = i;
                used = 0;
                page_rows = 0;
            }
            used += row->height + (page_rows == 0 ? 0 : display->row_gap);
            ++page_rows;
            if (i == selected) display->page_index = display->page_count;
            row->address = optional_text(node, "address");
            row->subject = optional_text(node, "subject");
            if ((mpack_node_map_contains_cstr(node, "address") && row->address == NULL) ||
                (mpack_node_map_contains_cstr(node, "subject") && row->subject == NULL)) goto invalid;
            if (row->subject != NULL) {
                size_t length = strlen(row->subject);
                if (strncmp(row->secondary, row->subject, length) != 0) goto invalid;
            }
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(row->id, display->rows[j].id) == 0) goto invalid;
        }
    }
    if (mpack_node_error(page) != mpack_ok) goto invalid;
    return display;
invalid:
    spark_display_free(display);
    return NULL;
}

void spark_display_free(spark_display_t* display) {
    if (display == NULL) return;
    free(display->title);
    free(display->hint);
    for (size_t i = 0; i < display->count; ++i) {
        free(display->rows[i].id);
        free(display->rows[i].mark);
        free(display->rows[i].primary);
        free(display->rows[i].secondary);
        free(display->rows[i].meta);
        free(display->rows[i].address);
        free(display->rows[i].subject);
    }
    free(display);
}
