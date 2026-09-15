#include "display.h"

#include <stdlib.h>
#include <string.h>

#include "lvgl/src/libs/lodepng/lodepng.h"

static bool node_is(mpack_node_t node, const char* text) {
    return mpack_node_type(node) == mpack_type_str && mpack_node_strlen(node) == strlen(text) &&
           memcmp(mpack_node_str(node), text, strlen(text)) == 0;
}

static char* optional_text(mpack_node_t node, const char* key) {
    if (!mpack_node_map_contains_cstr(node, key)) return NULL;
    return mpack_node_utf8_cstr_alloc(mpack_node_map_cstr(node, key), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
}

static bool text_is_utf8(const unsigned char* text, size_t size) {
    while (size > 0) {
        unsigned char lead = *text++;
        --size;
        if (lead == 0) return false;
        if (lead < 0x80) continue;

        size_t continuation_count;
        uint32_t codepoint;
        uint32_t minimum;
        if ((lead & 0xE0) == 0xC0) {
            continuation_count = 1; codepoint = lead & 0x1F; minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            continuation_count = 2; codepoint = lead & 0x0F; minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            continuation_count = 3; codepoint = lead & 0x07; minimum = 0x10000;
        } else {
            return false;
        }
        if (size < continuation_count) return false;
        for (size_t i = 0; i < continuation_count; ++i) {
            unsigned char continuation = *text++;
            if ((continuation & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        size -= continuation_count;
        if (codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return false;
    }
    return true;
}

static int base64_value(unsigned char byte) {
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    if (byte == '+') return 62;
    if (byte == '/') return 63;
    return -1;
}

static unsigned char* base64_decode(mpack_node_t node, size_t* output_size) {
    if (mpack_node_type(node) != mpack_type_str) return NULL;
    const unsigned char* input = (const unsigned char*)mpack_node_str(node);
    size_t input_size = mpack_node_strlen(node);
    if (input_size == 0 || input_size % 4 != 0) return NULL;
    size_t padding = input[input_size - 1] == '=' ? 1 : 0;
    if (input[input_size - 2] == '=') ++padding;
    size_t size = input_size / 4 * 3 - padding;
    if (size == 0 || size > SPARK_DISPLAY_MAX_TEXT) return NULL;
    unsigned char* output = malloc(size);
    if (output == NULL) return NULL;
    size_t offset = 0;
    for (size_t i = 0; i < input_size; i += 4) {
        int a = base64_value(input[i]);
        int b = base64_value(input[i + 1]);
        int c = input[i + 2] == '=' ? 0 : base64_value(input[i + 2]);
        int d = input[i + 3] == '=' ? 0 : base64_value(input[i + 3]);
        bool last = i + 4 == input_size;
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (input[i + 2] == '=' && (!last || input[i + 3] != '=' || (b & 15) != 0)) ||
            (input[i + 3] == '=' && (!last || (c & 3) != 0))) {
            free(output);
            return NULL;
        }
        output[offset++] = (unsigned char)((a << 2) | (b >> 4));
        if (offset < size) output[offset++] = (unsigned char)((b << 4) | (c >> 2));
        if (offset < size) output[offset++] = (unsigned char)((c << 6) | d);
    }
    *output_size = size;
    return output;
}

static char* item_body(mpack_node_t node) {
    if (mpack_node_type(node) == mpack_type_str)
        return mpack_node_utf8_cstr_alloc(node, SPARK_DISPLAY_MAX_TEXT + 1);
    if (mpack_node_type(node) != mpack_type_array || mpack_node_array_length(node) != 2)
        return NULL;
    uint32_t output_size = mpack_node_u32(mpack_node_array_at(node, 0));
    mpack_node_t bytes = mpack_node_array_at(node, 1);
    if (output_size == 0 || output_size > SPARK_DISPLAY_MAX_TEXT) return NULL;
    size_t input_size = 0;
    unsigned char* input = base64_decode(bytes, &input_size);
    if (input == NULL) return NULL;
    if (mpack_node_error(node) != mpack_ok) {
        free(input);
        return NULL;
    }
    unsigned char* inflated = NULL;
    size_t inflated_size = 0;
    LodePNGDecompressSettings settings;
    lodepng_decompress_settings_init(&settings);
    settings.max_output_size = output_size;
    unsigned error = lodepng_inflate(
        &inflated, &inflated_size, input, input_size, &settings);
    free(input);
    if (error != 0 || inflated_size != output_size || !text_is_utf8(inflated, output_size)) {
        lv_free(inflated);
        return NULL;
    }
    char* text = malloc(output_size + 1);
    if (text == NULL) {
        lv_free(inflated);
        return NULL;
    }
    memcpy(text, inflated, output_size);
    text[output_size] = '\0';
    lv_free(inflated);
    return text;
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
    display->kind = is_list ? SPARK_DISPLAY_LIST : SPARK_DISPLAY_DETAIL;
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
                row->layout == SPARK_LAYOUT_NOTE ? 48 :
                60;
            if (row->height != expected ||
                (row->layout == SPARK_LAYOUT_EVENT && row->meta[0] != '\0')) goto invalid;
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

static spark_display_t* single_row(spark_display_kind_t kind, mpack_node_t id) {
    spark_display_t* display = calloc(1, sizeof(*display));
    if (display == NULL) return NULL;
    display->kind = kind;
    display->count = 1;
    display->rows[0].layout = SPARK_LAYOUT_DETAIL;
    display->rows[0].id = mpack_node_utf8_cstr_alloc(id, 257);
    if (spark_text_empty(display->rows[0].id)) {
        spark_display_free(display);
        return NULL;
    }
    return display;
}

spark_display_t* spark_display_parse_item(mpack_node_t item) {
    if (mpack_node_type(item) != mpack_type_array || mpack_node_array_length(item) != 6)
        return NULL;
    spark_display_t* display = single_row(SPARK_DISPLAY_DETAIL, mpack_node_array_at(item, 0));
    if (display == NULL) return NULL;
    display->title = mpack_node_utf8_cstr_alloc(
        mpack_node_array_at(item, 1), SPARK_DISPLAY_MAX_TEXT + 1);
    display->hint = mpack_node_utf8_cstr_alloc(
        mpack_node_array_at(item, 2), SPARK_DISPLAY_MAX_TEXT + 1);
    display->rows[0].primary = mpack_node_utf8_cstr_alloc(
        mpack_node_array_at(item, 3), SPARK_DISPLAY_MAX_TEXT + 1);
    display->rows[0].secondary = item_body(mpack_node_array_at(item, 4));
    display->rows[0].meta = mpack_node_utf8_cstr_alloc(
        mpack_node_array_at(item, 5), SPARK_DISPLAY_MAX_TEXT + 1);
    if (display->title == NULL || display->hint == NULL ||
        display->rows[0].primary == NULL || display->rows[0].secondary == NULL ||
        display->rows[0].meta == NULL || mpack_node_error(item) != mpack_ok) {
        spark_display_free(display);
        return NULL;
    }
    return display;
}

// `[id, [[tag, text], ...]]`: one through 24 blocks, tag "h" or "p", text not empty.
spark_display_t* spark_display_parse_doc(mpack_node_t doc) {
    if (mpack_node_type(doc) != mpack_type_array || mpack_node_array_length(doc) != 2)
        return NULL;
    mpack_node_t blocks = mpack_node_array_at(doc, 1);
    if (mpack_node_type(blocks) != mpack_type_array) return NULL;
    size_t count = mpack_node_array_length(blocks);
    if (count == 0 || count > SPARK_DISPLAY_MAX_BLOCKS) return NULL;
    spark_display_t* display = single_row(SPARK_DISPLAY_DOC, mpack_node_array_at(doc, 0));
    if (display == NULL) return NULL;
    spark_display_doc_t* body = &display->body.doc;
    body->count = count;
    for (size_t i = 0; i < count; ++i) {
        mpack_node_t block = mpack_node_array_at(blocks, i);
        if (mpack_node_type(block) != mpack_type_array || mpack_node_array_length(block) != 2)
            goto invalid;
        mpack_node_t tag = mpack_node_array_at(block, 0);
        if (node_is(tag, "h")) body->heading[i] = true;
        else if (!node_is(tag, "p")) goto invalid;
        body->text[i] = mpack_node_utf8_cstr_alloc(
            mpack_node_array_at(block, 1), SPARK_DISPLAY_MAX_TEXT + 1);
        if (spark_text_empty(body->text[i])) goto invalid;
    }
    if (mpack_node_error(doc) != mpack_ok) goto invalid;
    return display;
invalid:
    spark_display_free(display);
    return NULL;
}

// `[id, headers, rows]`: two or three column headings (empty strings allowed)
// and one through twenty rows with exactly one cell per column.
spark_display_t* spark_display_parse_grid(mpack_node_t grid) {
    if (mpack_node_type(grid) != mpack_type_array || mpack_node_array_length(grid) != 3)
        return NULL;
    mpack_node_t headers = mpack_node_array_at(grid, 1);
    mpack_node_t rows = mpack_node_array_at(grid, 2);
    if (mpack_node_type(headers) != mpack_type_array || mpack_node_type(rows) != mpack_type_array)
        return NULL;
    size_t columns = mpack_node_array_length(headers);
    size_t row_count = mpack_node_array_length(rows);
    if (columns < SPARK_DISPLAY_MIN_COLUMNS || columns > SPARK_DISPLAY_MAX_COLUMNS ||
        row_count == 0 || row_count > SPARK_DISPLAY_MAX_GRID_ROWS)
        return NULL;
    spark_display_t* display = single_row(SPARK_DISPLAY_GRID, mpack_node_array_at(grid, 0));
    if (display == NULL) return NULL;
    spark_display_grid_t* body = &display->body.grid;
    body->column_count = columns;
    body->row_count = row_count;
    for (size_t c = 0; c < columns; ++c) {
        body->headers[c] = mpack_node_utf8_cstr_alloc(
            mpack_node_array_at(headers, c), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
        if (body->headers[c] == NULL) goto invalid;
    }
    for (size_t r = 0; r < row_count; ++r) {
        mpack_node_t row = mpack_node_array_at(rows, r);
        if (mpack_node_type(row) != mpack_type_array || mpack_node_array_length(row) != columns)
            goto invalid;
        for (size_t c = 0; c < columns; ++c) {
            body->cells[r][c] = mpack_node_utf8_cstr_alloc(
                mpack_node_array_at(row, c), SPARK_DISPLAY_MAX_LIST_TEXT + 1);
            if (body->cells[r][c] == NULL) goto invalid;
        }
    }
    if (mpack_node_error(grid) != mpack_ok) goto invalid;
    return display;
invalid:
    spark_display_free(display);
    return NULL;
}

void spark_display_free(spark_display_t* display) {
    if (display == NULL) return;
    free(display->title);
    free(display->hint);
    if (display->kind == SPARK_DISPLAY_DOC) {
        for (size_t i = 0; i < display->body.doc.count; ++i) free(display->body.doc.text[i]);
    } else if (display->kind == SPARK_DISPLAY_GRID) {
        spark_display_grid_t* grid = &display->body.grid;
        for (size_t c = 0; c < grid->column_count; ++c) {
            free(grid->headers[c]);
            for (size_t r = 0; r < grid->row_count; ++r) free(grid->cells[r][c]);
        }
    }
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
