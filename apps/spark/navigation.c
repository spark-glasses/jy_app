#include "spark.h"
#include "view_internal.h"

#include <string.h>

static spark_display_t* s_display;
static spark_display_t* s_return_list;
static bool s_open_pending;

const spark_display_t* spark_display_current(void) {
    return s_display;
}

static bool same_text(const char* a, const char* b) {
    return strcmp(spark_text(a), spark_text(b)) == 0;
}

static bool same_list(const spark_display_t* a, const spark_display_t* b) {
    if (a == NULL || a->kind != SPARK_DISPLAY_LIST || b->kind != SPARK_DISPLAY_LIST ||
        a->count != b->count || a->page_count != b->page_count || a->row_gap != b->row_gap ||
        !same_text(a->title, b->title) || !same_text(a->hint, b->hint)) return false;
    if (memcmp(a->page_starts, b->page_starts, sizeof(a->page_starts)) != 0) return false;
    for (size_t i = 0; i < a->count; ++i) {
        const spark_display_row_t* x = &a->rows[i];
        const spark_display_row_t* y = &b->rows[i];
        if (x->layout != y->layout || x->height != y->height || !same_text(x->id, y->id) ||
            !same_text(x->mark, y->mark) || !same_text(x->primary, y->primary) ||
            !same_text(x->secondary, y->secondary) || !same_text(x->meta, y->meta) ||
            !same_text(x->address, y->address) || !same_text(x->subject, y->subject)) return false;
    }
    return true;
}

static bool showing(void) {
    return s_display != NULL && s_display->count != 0;
}

static void select_index(size_t next) {
    size_t old = s_display->selected;
    if (old == next) return;
    s_open_pending = false;
    size_t old_page = s_display->page_index - 1, page = 0;
    while (page + 1 < s_display->page_count && next >= s_display->page_starts[page + 1]) ++page;
    s_display->selected = next;
    s_display->page_index = page + 1;
    if (page != old_page) {
        spark_view_render(s_display);
    } else {
        size_t start = s_display->page_starts[page];
        spark_body_list_select(old - start, &s_display->rows[old], false);
        spark_body_list_select(next - start, &s_display->rows[next], true);
    }
    spark_display_paint();
}

bool spark_display_is_selected(const char* id) {
    return showing() && strcmp(s_display->rows[s_display->selected].id, id) == 0;
}

bool spark_display_apply(spark_display_t* display, bool new_display) {
    if (!spark_display_ready() || display == NULL) return false;
    if (new_display) s_open_pending = false;
    if (same_list(s_display, display)) {
        select_index(display->selected);
        spark_display_free(display);
        return true;
    }
    s_open_pending = false;
    spark_display_t* old = s_display;
    if (new_display || display->kind == SPARK_DISPLAY_LIST) {
        spark_display_free(s_return_list);
        s_return_list = NULL;
    } else if (old != NULL && old->kind == SPARK_DISPLAY_LIST) {
        s_return_list = old;
        old = NULL;
    }
    spark_view_render(display);
    s_display = display;
    spark_display_free(old);
    return true;
}

void spark_navigation_move(bool forward) {
    if (!showing()) return;
    if (s_display->kind != SPARK_DISPLAY_LIST) {
        spark_view_scroll(forward);
        spark_display_paint();
        return;
    }
    s_open_pending = false;
    size_t next = s_display->selected;
    if (forward && next + 1 < s_display->count) ++next;
    if (!forward && next > 0) --next;
    select_index(next);
    spark_display_report("selected", s_display->rows[next].id);
}

void spark_navigation_open(void) {
    if (!showing() || s_display->kind != SPARK_DISPLAY_LIST || s_open_pending) return;
    s_open_pending = spark_display_report("open", s_display->rows[s_display->selected].id);
}

void spark_navigation_back(void) {
    if (!showing() || s_display->kind == SPARK_DISPLAY_LIST || s_return_list == NULL) return;
    s_open_pending = false;
    spark_display_t* old = s_display;
    s_display = s_return_list;
    s_return_list = NULL;
    spark_view_render(s_display);
    spark_display_free(old);
    spark_display_paint();
    spark_display_report("selected", s_display->rows[s_display->selected].id);
}

void spark_navigation_dismiss(void) {
    if (!showing() && !spark_reply_visible()) return;
    spark_navigation_clear();
    (void)spark_reply_set("");
    spark_display_paint();
    spark_display_report("dismissed", NULL);
}

void spark_navigation_clear(void) {
    s_open_pending = false;
    spark_view_render(NULL);
    spark_display_free(s_display);
    s_display = NULL;
    spark_display_free(s_return_list);
    s_return_list = NULL;
}
