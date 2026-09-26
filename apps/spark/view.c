#include "spark.h"
#include "assistant_avatar.h"
#include "view_internal.h"

#include "floatair_dbg.h"
#include "system/system_runtime_state.h"
#include "system/system_runtime_ui.h"

#include <stdio.h>

#define SPARK_FRAME_MARGIN_H 8
#define SPARK_FRAME_MARGIN_V 4
#define SPARK_FRAME_RADIUS 20
#define SPARK_FRAME_INSET_H 24
#define SPARK_HEADER_HEIGHT 64
#define SPARK_LIST_HEADER_HEIGHT 44
#define SPARK_FOOTER_HEIGHT 84
#define SPARK_AVATAR_LEFT 30
#define SPARK_AVATAR_SIZE 40
#define SPARK_AVATAR_BOTTOM 22

static lv_obj_t* s_frame;
static lv_obj_t* s_header;
static lv_obj_t* s_title;
static lv_obj_t* s_hint;
static lv_obj_t* s_pages;
static lv_obj_t* s_content;
static lv_obj_t* s_footer;
static spark_assistant_avatar_t* s_avatar;
static bool s_ready;
static int s_shown = -1;

static const spark_body_t* const s_bodies[SPARK_BODY_COUNT] = {
    [SPARK_BODY_LIST] = &spark_body_list,
    [SPARK_BODY_DETAIL] = &spark_body_detail,
    [SPARK_BODY_GRID] = &spark_body_grid,
    [SPARK_BODY_DOC] = &spark_body_doc,
};
static const spark_body_kind_t s_creation_order[SPARK_BODY_COUNT] = {
    SPARK_BODY_DETAIL, SPARK_BODY_LIST, SPARK_BODY_GRID, SPARK_BODY_DOC,
};

static spark_body_kind_t body_kind(const spark_display_t* display) {
    if (display->kind == SPARK_DISPLAY_LIST) return SPARK_BODY_LIST;
    const spark_display_card_t* card = display->items[0].card;
    if (card == NULL) return SPARK_BODY_DETAIL;
    return card->is_grid ? SPARK_BODY_GRID : SPARK_BODY_DOC;
}

static void set_header_height(int32_t height) {
    // Untitled pages have no header band; the body starts at the frame top.
    lv_obj_update_flag(s_header, LV_OBJ_FLAG_HIDDEN, height == 0);
    lv_obj_set_height(s_header, height);
    lv_obj_set_pos(s_content, 0, height + 6);
    int32_t available = lv_obj_get_content_height(s_frame) - height - 8;
    lv_obj_set_size(s_content, LV_PCT(100), LV_MAX(1,
        height == SPARK_LIST_HEADER_HEIGHT ? LV_MIN(available, SPARK_DISPLAY_CONTENT_HEIGHT) : available));
}

static void render_header(const spark_display_t* display, const char* title, const char* hint) {
    int32_t width = lv_obj_get_content_width(s_header);
    bool list = display->kind == SPARK_DISPLAY_LIST;
    char pages[48] = "";
    if (list && display->page_count > 1)
        snprintf(pages, sizeof(pages), "%s%u/%u%s", display->page_index > 1 ? "< " : "",
                 (unsigned)display->page_index, (unsigned)display->page_count,
                 display->page_index < display->page_count ? " >" : "");
    int32_t page_width = pages[0] == '\0' ? 0 : LV_MIN(spark_text_width(s_pages, pages), width / 3);
    spark_line(s_pages, pages, width - page_width, 10, page_width);
    int32_t available = width - (page_width ? page_width + 12 : 0);
    int32_t title_width = list ? LV_MIN(spark_text_width(s_title, title), available * 2 / 3) : available;
    spark_line(s_title, title, 0, 8, title_width);
    lv_obj_set_height(s_title, 32);
    const lv_font_t* title_font = lv_obj_get_style_text_font(s_title, LV_PART_MAIN);
    const lv_font_t* hint_font = lv_obj_get_style_text_font(s_hint, LV_PART_MAIN);
    int32_t hint_y = 8 + title_font->line_height - title_font->base_line
                       - hint_font->line_height + hint_font->base_line;
    if (list) spark_line(s_hint, hint, title_width + 10, hint_y, available - title_width - 10);
    else spark_line(s_hint, hint, 0, 38, width);
}

/*
 * The periodic UI tick would render these changes up to LV_DEF_REFR_PERIOD
 * later and mark the whole screen dirty on the way. Painting here keeps the
 * ACK after the pixels.
 */
void spark_display_paint(void) {
    if (!s_ready) return;
    (void)system_ui_paint_now();
}

void spark_view_render(const spark_display_t* display) {
    if (!s_ready) return;
    bool visible = display != NULL && display->count != 0;
    bool is_list = visible && display->kind == SPARK_DISPLAY_LIST;
    const char* title = NULL;
    const char* hint = NULL;
    if (is_list) {
        title = display->title;
        hint = display->hint;
    } else if (visible) {
        spark_item_heading(&display->items[0], &title, &hint);
    }
    bool untitled = visible && !is_list && spark_text_empty(title) && spark_text_empty(hint);
    set_header_height(is_list ? SPARK_LIST_HEADER_HEIGHT : untitled ? 0 : SPARK_HEADER_HEIGHT);
    lv_label_set_text_static(s_title, "");
    lv_label_set_text_static(s_hint, "");
    if (s_shown >= 0) s_bodies[s_shown]->reset();
    s_shown = -1;
    if (!visible) {
        lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!untitled) render_header(display, title, hint);
    lv_obj_update_flag(s_content, LV_OBJ_FLAG_SCROLLABLE, !is_list);
    spark_body_kind_t body = body_kind(display);
    s_bodies[body]->render(display);
    s_shown = (int)body;
    lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_frame);
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    lv_obj_invalidate(s_frame);
}

void spark_view_scroll(bool forward) {
    if (!s_ready) return;
    int32_t step = lv_obj_get_content_height(s_content) / 2;
    lv_obj_scroll_by(s_content, 0, forward ? -step : step, LV_ANIM_OFF);
}

bool spark_display_ready(void) { return s_ready; }

void spark_display_clear(void) {
    spark_display_reset_revision();
    (void)spark_assistant_apply(spark_assistant_current());
    (void)spark_reply_set("");
    spark_navigation_clear();
    system_ui_request_screen_refresh();
}

static void spark_input(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_GESTURE_LEFT || code == LV_EVENT_GESTURE_RIGHT)
        spark_navigation_move(code == LV_EVENT_GESTURE_LEFT);
    else if (code == LV_EVENT_CLICKED) spark_navigation_open();
    else if (code == LV_EVENT_DCLICKED) spark_navigation_back();
    else if (code == system_runtime_state_get_btconn_event()) {
        const uint8_t* connected = lv_event_get_param(event);
        if (connected != NULL) spark_host_connection_changed(*connected != 0);
    }
}

static void spark_page_create(lv_obj_t* parent, const app_page_data_t* data) {
    (void)data;
    lv_obj_add_event_cb(parent, spark_input, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(parent);

    lv_obj_t* frame = lv_obj_create(parent);
    floatair_assert(frame != NULL, "Spark display frame create failed");
    if (frame == NULL) return;

    s_frame = frame;
    spark_prepare_static_obj(frame);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(frame, spark_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(frame, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_size(
        frame,
        LV_MAX(1, lv_obj_get_content_width(parent) - SPARK_FRAME_MARGIN_H * 2),
        LV_MAX(1, lv_obj_get_content_height(parent) - SPARK_FOOTER_HEIGHT -
                      SPARK_FRAME_MARGIN_V * 2));
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, SPARK_FRAME_MARGIN_V);
    lv_obj_set_style_border_color(frame, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(frame, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(frame, SPARK_FRAME_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(frame, SPARK_FRAME_INSET_H, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(frame, 0, LV_PART_MAIN);

    s_header = lv_obj_create(frame);
    floatair_assert(s_header != NULL, "Spark display header create failed");
    if (s_header == NULL) return;

    spark_prepare_static_obj(s_header);
    lv_obj_set_size(s_header, LV_PCT(100), SPARK_HEADER_HEIGHT);
    lv_obj_align(s_header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_border_color(s_header, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_header, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_header, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_header, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);

    s_title = lv_label_create(s_header);
    s_hint = lv_label_create(s_header);
    s_pages = lv_label_create(s_header);
    s_content = lv_obj_create(frame);
    if (s_title == NULL || s_hint == NULL || s_pages == NULL || s_content == NULL) return;
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(s_pages, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_title, spark_font(18), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_pages, spark_font(14), LV_PART_MAIN);
    lv_obj_set_size(s_title, LV_PCT(100), 40);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_set_style_text_font(s_hint, spark_font(12), LV_PART_MAIN);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    spark_prepare_static_obj(s_content);
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(frame);
    lv_obj_set_size(s_content, LV_PCT(100), LV_MAX(1, lv_obj_get_content_height(frame) - SPARK_HEADER_HEIGHT));
    lv_obj_set_pos(s_content, 0, SPARK_HEADER_HEIGHT);
    lv_obj_update_layout(s_content);
    for (size_t i = 0; i < SPARK_BODY_COUNT; ++i)
        if (!s_bodies[s_creation_order[i]]->create(s_content)) return;

    s_footer = lv_obj_create(parent);
    if (s_footer == NULL) return;
    spark_prepare_static_obj(s_footer);
    lv_obj_set_size(s_footer, LV_PCT(100), SPARK_FOOTER_HEIGHT);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_avatar = spark_assistant_avatar_create(s_footer, SPARK_AVATAR_SIZE);
    lv_obj_t* avatar_obj = spark_assistant_avatar_object(s_avatar);
    if (s_avatar == NULL || avatar_obj == NULL) return;
    lv_obj_align(avatar_obj, LV_ALIGN_BOTTOM_LEFT, SPARK_AVATAR_LEFT, -SPARK_AVATAR_BOTTOM);

    if (!spark_reply_create(s_footer)) return;
    spark_assistant_avatar_roll_in(s_avatar);

    s_ready = true;
    spark_view_render(spark_display_current());
}

static void spark_page_destroy(void) {
    s_ready = false;
    s_shown = -1;
    s_frame = s_header = s_title = s_hint = s_pages = s_content = NULL;
    for (size_t i = 0; i < SPARK_BODY_COUNT; ++i) s_bodies[i]->destroy();
    s_footer = NULL;
    s_avatar = NULL;
    spark_reply_destroy();
}

static app_page_t s_spark_page = {
    .name = "spark",
    .on_create = spark_page_create,
    .on_destroy = spark_page_destroy,
    .on_unload = spark_display_clear,
};

app_page_t* spark_page_get(void) {
    return &s_spark_page;
}

bool spark_assistant_apply(const spark_assistant_presentation_t* presentation) {
    if (!s_ready || s_avatar == NULL || presentation == NULL) return false;
    return spark_assistant_avatar_set_state(s_avatar, presentation);
}
