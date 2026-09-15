#include "view_internal.h"

#include <string.h>

#define SPARK_GRID_TOP 8
#define SPARK_GRID_COLUMN_GAP 16
#define SPARK_GRID_ROW_GAP 6
#define SPARK_GRID_HEADER_GAP 4
#define SPARK_GRID_RULE_GAP 8
#define SPARK_GRID_HEADER_FONT 18

static lv_obj_t* s_grid;
static lv_obj_t* s_headers[SPARK_DISPLAY_MAX_COLUMNS];
static lv_obj_t* s_rule;
static lv_obj_t* s_cells[SPARK_DISPLAY_MAX_GRID_ROWS][SPARK_DISPLAY_MAX_COLUMNS];

static lv_obj_t* cell(size_t row, size_t column) {
    if (s_cells[row][column] == NULL)
        s_cells[row][column] = spark_wrap_label(s_grid, SPARK_TEXT_FONT_SIZE);
    return s_cells[row][column];
}

static bool create(lv_obj_t* content) {
    s_grid = lv_obj_create(content);
    if (s_grid == NULL) return false;
    spark_prepare_static_obj(s_grid);
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_grid, 0, 0);
    lv_obj_set_size(s_grid, LV_PCT(100), 1);
    for (size_t c = 0; c < SPARK_DISPLAY_MAX_COLUMNS; ++c) {
        s_headers[c] = spark_wrap_label(s_grid, SPARK_GRID_HEADER_FONT);
        if (s_headers[c] == NULL) return false;
        lv_obj_set_style_text_line_space(s_headers[c], 0, LV_PART_MAIN);
    }
    s_rule = lv_obj_create(s_grid);
    if (s_rule == NULL) return false;
    spark_prepare_static_obj(s_rule);
    lv_obj_set_style_bg_opa(s_rule, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rule, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void reset(void) {
    for (size_t c = 0; c < SPARK_DISPLAY_MAX_COLUMNS; ++c) {
        spark_detach(s_headers[c]);
        for (size_t r = 0; r < SPARK_DISPLAY_MAX_GRID_ROWS; ++r) spark_detach(s_cells[r][c]);
    }
    lv_obj_add_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
}

static void render(const spark_display_t* display) {
    const spark_display_grid_t* grid = &display->body.grid;
    lv_obj_t* content = lv_obj_get_parent(s_grid);
    lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(content);
    int32_t width = lv_obj_get_content_width(content);
    int32_t columns = (int32_t)grid->column_count;
    int32_t column_width = LV_MAX(1, (width - SPARK_GRID_COLUMN_GAP * (columns - 1)) / columns);
    int32_t y = SPARK_GRID_TOP;
    bool has_headers = false;
    for (int32_t c = 0; c < columns; ++c) has_headers |= !spark_text_empty(grid->headers[c]);
    if (has_headers) {
        int32_t header_height = 0;
        for (int32_t c = 0; c < columns; ++c)
            header_height = LV_MAX(header_height, spark_place(
                s_headers[c], grid->headers[c], c * (column_width + SPARK_GRID_COLUMN_GAP), y, column_width));
        y += header_height + SPARK_GRID_HEADER_GAP;
        lv_obj_remove_flag(s_rule, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_rule, 0, y);
        lv_obj_set_size(s_rule, width, 1);
        y += 1 + SPARK_GRID_RULE_GAP;
    }
    for (size_t r = 0; r < grid->row_count; ++r) {
        int32_t row_height = 0;
        for (int32_t c = 0; c < columns; ++c) {
            lv_obj_t* label = cell(r, (size_t)c);
            if (label == NULL) return;
            row_height = LV_MAX(row_height, spark_place(
                label, grid->cells[r][c], c * (column_width + SPARK_GRID_COLUMN_GAP), y, column_width));
        }
        y += row_height + SPARK_GRID_ROW_GAP;
    }
    lv_obj_set_size(s_grid, width, y);
}

static void destroy(void) {
    s_grid = s_rule = NULL;
    memset(s_headers, 0, sizeof(s_headers));
    memset(s_cells, 0, sizeof(s_cells));
}

const spark_body_t spark_body_grid = {create, reset, render, destroy};
