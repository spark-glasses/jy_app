#include "view_internal.h"

#include <stdlib.h>

#define SPARK_DETAIL_TOP 8
#define SPARK_DETAIL_GAP 12

static lv_obj_t* s_lead;
static lv_obj_t* s_body;
static lv_obj_t* s_meta;

static lv_obj_t* label(lv_obj_t* content, unsigned size) {
    lv_obj_t* obj = lv_label_create(content);
    if (obj == NULL) return NULL;
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_font(obj, spark_font(size), LV_PART_MAIN);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

static bool create(lv_obj_t* content) {
    s_lead = label(content, 18);
    s_body = label(content, SPARK_TEXT_FONT_SIZE);
    s_meta = label(content, 14);
    if (s_lead == NULL || s_body == NULL || s_meta == NULL) return false;
    lv_obj_set_y(s_lead, SPARK_DETAIL_TOP);
    lv_obj_set_style_text_line_space(s_body, SPARK_TEXT_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_meta, lv_color_make(170, 170, 170), LV_PART_MAIN);
    return true;
}

static void reset(void) {
    lv_obj_t* labels[] = {s_lead, s_body, s_meta};
    for (size_t i = 0; i < 3; ++i) {
        lv_label_set_text_static(labels[i], "");
        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void spark_item_heading(const spark_display_item_t* item, const char** title, const char** hint) {
    *hint = item->has_more ? "More available" : "";
    switch (item->type) {
    case SPARK_ITEM_TODO: *title = "Reminder"; break;
    case SPARK_ITEM_EMAIL:
    case SPARK_ITEM_EMAIL_DRAFT: *title = spark_item_text(item, "subject"); break;
    case SPARK_ITEM_CONTACT:
    case SPARK_ITEM_PLACES: *title = spark_item_text(item, "name"); break;
    case SPARK_ITEM_NOTE:
    case SPARK_ITEM_CALENDAR_EVENT:
    case SPARK_ITEM_ROUTE: *title = spark_item_text(item, "title"); break;
    case SPARK_ITEM_CARD:
    case SPARK_ITEM_TYPE_COUNT:
        *title = "";
        *hint = "";
        break;
    }
}

// A lead line, a wrapping body, and a grey line under it; each type fills them.
static void render(const spark_display_t* display) {
    const spark_display_item_t* item = &display->items[0];
    const char* lead = NULL;
    const char* body = NULL;
    const char* meta = NULL;
    char* made_lead = NULL;
    char* made_body = NULL;
    switch (item->type) {
    case SPARK_ITEM_TODO: {
        const char* when[] = {spark_item_text(item, "due"), spark_item_text(item, "repeat")};
        lead = spark_item_text(item, "content");
        body = made_body = spark_joined(when, 2, " · ");
        meta = spark_item_flag(item, "completed") ? "Completed" : "Open";
        break;
    }
    case SPARK_ITEM_NOTE:
        body = spark_item_text(item, "content");
        meta = spark_item_text(item, "date");
        break;
    case SPARK_ITEM_EMAIL:
        lead = spark_item_text(item, "sender");
        body = spark_item_text(item, "preview");
        meta = spark_item_text(item, "sentAt");
        break;
    case SPARK_ITEM_EMAIL_DRAFT:
        lead = spark_item_text(item, "to");
        body = spark_item_text(item, "body");
        meta = spark_item_text(item, "status");
        break;
    case SPARK_ITEM_CALENDAR_EVENT:
        lead = spark_item_text(item, "when");
        body = spark_item_text(item, "location");
        meta = spark_item_text(item, "response");
        break;
    case SPARK_ITEM_CONTACT: {
        const char* work[] = {spark_item_text(item, "jobTitle"), spark_item_text(item, "organization")};
        const char* reach[] = {spark_item_text(item, "phone"), spark_item_text(item, "email")};
        lead = made_lead = spark_joined(work, 2, " · ");
        body = made_body = spark_joined(reach, 2, "\n");
        break;
    }
    case SPARK_ITEM_PLACES:
        lead = spark_item_text(item, "address");
        body = spark_item_text(item, "rating");
        meta = spark_item_text(item, "open");
        break;
    case SPARK_ITEM_ROUTE: {
        const char* trip[] = {spark_item_text(item, "duration"), spark_item_text(item, "distance")};
        lead = made_lead = spark_joined(trip, 2, " · ");
        body = spark_item_text(item, "via");
        meta = spark_item_text(item, "mode");
        break;
    }
    case SPARK_ITEM_CARD:
    case SPARK_ITEM_TYPE_COUNT:
        break;
    }
    lv_obj_t* parent = lv_obj_get_parent(s_lead);
    lv_obj_update_layout(parent);
    int32_t width = lv_obj_get_content_width(parent);
    lv_label_set_text(s_lead, spark_text(lead));
    lv_label_set_text(s_body, spark_text(body));
    lv_label_set_text(s_meta, spark_text(meta));
    free(made_lead);
    free(made_body);
    lead = lv_label_get_text(s_lead);
    body = lv_label_get_text(s_body);
    lv_obj_remove_flag(s_lead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_meta, LV_OBJ_FLAG_HIDDEN);
    int32_t body_y = spark_text_empty(lead) ? SPARK_DETAIL_TOP :
        SPARK_DETAIL_TOP + spark_text_height(s_lead, lead, width) + SPARK_DETAIL_GAP;
    lv_obj_set_y(s_body, body_y);
    lv_obj_set_y(s_meta, body_y + spark_text_height(s_body, body, width) + SPARK_DETAIL_GAP);
}

static void destroy(void) {
    s_lead = s_body = s_meta = NULL;
}

const spark_body_t spark_body_detail = {create, reset, render, destroy};
