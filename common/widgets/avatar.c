#include "avatar.h"
#include "floatair_dbg.h"
#include "lvgl/src/draw/lv_draw_private.h"
#include "lvgl/src/draw/sw/blend/lv_draw_sw_blend_private.h"

#define AVATAR_LISTENING_MIN_OPA 180
#define AVATAR_LISTENING_MAX_SIZE_PERCENT 120
#define AVATAR_LISTENING_HALF_CYCLE_MS 650
#define AVATAR_ENTRANCE_DURATION_MS 450
#define AVATAR_EXIT_DURATION_MS 300
#define AVATAR_MIN_SIZE 6

/* Ordered monochrome dither. One software draw task blends each row;
 * no image source, decoded buffer, or image cache is needed. */
static const uint8_t avatar_bayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

struct avatar_t {
    ui_widget_t base;
    lv_obj_t* ball;
    int32_t size;
    int32_t angle;
    lv_opa_t brightness;
    bool draw_logged;
    avatar_state_t state;
    avatar_animation_complete_cb_t motion_completion;
    void* motion_user_data;
    int32_t motion_start_x;
    int32_t motion_progress;
    bool exiting;
    uint32_t motion_started_ms;
    uint32_t motion_last_update_ms;
    uint32_t motion_max_gap_ms;
    uint32_t motion_update_count;
};

typedef struct {
    lv_draw_custom_dsc_t custom;
    int32_t size;
    int32_t angle;
    lv_opa_t brightness;
    bool log_draw;
} avatar_draw_dsc_t;

static void avatar_draw_rows(lv_draw_unit_t* unit, const void* descriptor, const lv_area_t* bounds) {
    const avatar_draw_dsc_t* avatar = descriptor;
    int32_t size = avatar->size;
    int32_t radius_squared = size * size;
    int32_t sine = lv_trigo_sin(avatar->angle);
    int32_t cosine = lv_trigo_sin(avatar->angle + 90);
    uint32_t started = lv_tick_get();
    /* Chunk larger avatars too, without a variable-size stack allocation. */
    lv_opa_t row[40];
    lv_draw_sw_blend_dsc_t blend = {0};
    blend.color = lv_color_white();
    blend.opa = LV_OPA_COVER;
    blend.mask_buf = row;
    blend.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;

    for (int32_t y = 0; y < size; y++) {
        for (int32_t first_x = 0; first_x < size; first_x += (int32_t)sizeof(row)) {
            int32_t width = LV_MIN((int32_t)sizeof(row), size - first_x);
            for (int32_t i = 0; i < width; i++) {
                int32_t x = first_x + i;
                int32_t dx = 2 * x + 1 - size;
                int32_t dy = 2 * y + 1 - size;
                int32_t depth = radius_squared - dx * dx - dy * dy;
                bool lit = false;
                if (depth > 0) {
                    int32_t rx = (dx * cosine + dy * sine) >> LV_TRIGO_SHIFT;
                    int32_t ry = (dy * cosine - dx * sine) >> LV_TRIGO_SHIFT;
                    int32_t light = LV_CLAMP(32, 148 - (rx + ry) * 50 / size + depth * 44 / radius_squared, 224);
                    int32_t coverage = LV_MIN(255, depth * 255 / (4 * size));
                    /* Keep dots fixed while the shading rotates. */
                    lit = light * coverage / 255 > avatar_bayer[y & 3][x & 3] * 16 + 8;
                }
                /* Put brightness in the mask to avoid multiplying opacity twice. */
                row[i] = lit ? avatar->brightness : 0;
            }
            lv_area_t area = {
                .x1 = bounds->x1 + first_x, .y1 = bounds->y1 + y,
                .x2 = bounds->x1 + first_x + width - 1, .y2 = bounds->y1 + y,
            };
            blend.blend_area = &area;
            blend.mask_area = &area;
            blend.mask_stride = width;
            lv_draw_sw_blend(unit, &blend);
        }
    }
    if (avatar->log_draw) {
        floatair_info("avatar batched draw size=%ld row_bytes=%u cost_ms=%lu",
                      (long)size, (unsigned)sizeof(row), (unsigned long)lv_tick_elaps(started));
    }
}

static void avatar_on_draw(lv_event_t* event) {
    avatar_t* avatar = lv_event_get_user_data(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(avatar->ball, &bounds);
    avatar_draw_dsc_t* dsc = lv_malloc_zeroed(sizeof(*dsc));
    if (dsc == NULL) {
        return;
    }
    dsc->custom.base.dsc_size = sizeof(*dsc);
    dsc->custom.draw_cb = avatar_draw_rows;
    /* Snapshot values: the task does not retain the widget or a stack pointer. */
    dsc->size = lv_area_get_width(&bounds);
    dsc->angle = avatar->angle;
    dsc->brightness = avatar->brightness;
    dsc->log_draw = !avatar->draw_logged;
    lv_draw_task_t* task = lv_draw_add_task(layer, &bounds);
    task->type = LV_DRAW_TASK_TYPE_CUSTOM;
    task->draw_dsc = dsc;
    lv_draw_finalize_task_creation(layer, task);
    avatar->draw_logged = true;
}

static void avatar_trace_roll_start(avatar_t* avatar, uint32_t duration_ms) {
    floatair_info("avatar roll start avatar=%p direction=%s duration_ms=%lu",
                  (void*)avatar, avatar->exiting ? "exit" : "entrance",
                  (unsigned long)duration_ms);
    avatar->motion_started_ms = lv_tick_get();
    avatar->motion_last_update_ms = avatar->motion_started_ms;
    avatar->motion_max_gap_ms = 0;
    avatar->motion_update_count = 0;
}

static void avatar_trace_roll_end(avatar_t* avatar, const char* result) {
    floatair_info("avatar roll %s avatar=%p direction=%s elapsed_ms=%lu callbacks=%lu max_callback_gap_ms=%lu progress=%ld",
                  result, (void*)avatar, avatar->exiting ? "exit" : "entrance",
                  (unsigned long)lv_tick_elaps(avatar->motion_started_ms),
                  (unsigned long)avatar->motion_update_count,
                  (unsigned long)avatar->motion_max_gap_ms,
                  (long)avatar->motion_progress);
}

static void avatar_animate_listening(void* var, int32_t value) {
    avatar_t* avatar = var;
    if (lv_obj_is_visible(avatar->base.obj)) {
        int32_t max_size = avatar->size * AVATAR_LISTENING_MAX_SIZE_PERCENT / 100;
        int32_t size = avatar->size + (max_size - avatar->size) * value / 1000;
        lv_opa_t brightness = LV_OPA_COVER - (LV_OPA_COVER - AVATAR_LISTENING_MIN_OPA) * value / 1000;

        /* Resize the centered ball; keep its footer slot and roll position fixed. */
        lv_obj_set_size(avatar->ball, size, size);
        if (avatar->brightness != brightness) {
            avatar->brightness = brightness;
            lv_obj_invalidate(avatar->ball);
        }
    }
}

static void avatar_animate_roll(void* var, int32_t value) {
    avatar_t* avatar = var;
    int32_t remaining = 1000 - value;
    uint32_t now_ms = lv_tick_get();
    uint32_t gap_ms = now_ms - avatar->motion_last_update_ms;
    avatar->motion_max_gap_ms = LV_MAX(avatar->motion_max_gap_ms, gap_ms);
    avatar->motion_last_update_ms = now_ms;
    /* Includes LVGL's initial application; these are callbacks, not frames. */
    avatar->motion_update_count++;

    avatar->motion_progress = value;
    lv_obj_set_style_translate_x(avatar->base.obj,
                                 avatar->motion_start_x * remaining / 1000, LV_PART_MAIN);
    int32_t angle = value * 360 / 1000;
    if (angle != avatar->angle) {
        avatar->angle = angle;
        lv_obj_invalidate(avatar->ball);
    }
}

static void avatar_roll_complete(lv_anim_t* animation) {
    avatar_t* avatar = animation->var;
    avatar_animation_complete_cb_t completion = avatar->motion_completion;
    void* user_data = avatar->motion_user_data;

    avatar_trace_roll_end(avatar, "complete");
    avatar->motion_completion = NULL;
    avatar->motion_user_data = NULL;
    if (completion != NULL) {
        completion(avatar, user_data);
    }
}

static void avatar_apply_state(avatar_t* avatar) {
    lv_anim_delete(avatar, avatar_animate_listening);
    avatar->brightness = LV_OPA_COVER;
    lv_obj_set_size(avatar->ball, avatar->size, avatar->size);
    lv_obj_invalidate(avatar->ball);

    if (avatar->exiting || avatar->state != AVATAR_STATE_LISTENING ||
        ui_widget_is_hidden(UI_WIDGET(avatar))) {
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, avatar);
    lv_anim_set_exec_cb(&animation, avatar_animate_listening);
    lv_anim_set_values(&animation, 0, 1000);
    lv_anim_set_duration(&animation, AVATAR_LISTENING_HALF_CYCLE_MS);
    lv_anim_set_playback_duration(&animation, AVATAR_LISTENING_HALF_CYCLE_MS);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void avatar_on_delete(lv_event_t* event) {
    avatar_t* avatar = lv_event_get_user_data(event);

    if (lv_anim_get(avatar, avatar_animate_roll) != NULL) {
        avatar_trace_roll_end(avatar, "cancelled");
    }
    lv_anim_delete(avatar, avatar_animate_listening);
    lv_anim_delete(avatar, avatar_animate_roll);
    lv_obj_set_user_data(avatar->base.obj, NULL);
    lv_free(avatar);
}

avatar_t* avatar_create(lv_obj_t* parent, int32_t size) {
    if (size < AVATAR_MIN_SIZE) {
        return NULL;
    }
    if (parent == NULL) {
        parent = lv_screen_active();
    }
    if (parent == NULL) {
        return NULL;
    }

    avatar_t* avatar = lv_malloc_zeroed(sizeof(*avatar));
    if (avatar == NULL) {
        return NULL;
    }
    lv_obj_t* root = lv_obj_create(parent);
    if (root == NULL) {
        lv_free(avatar);
        return NULL;
    }
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(root, size, size);

    avatar->ball = lv_obj_create(root);
    if (avatar->ball == NULL) {
        lv_obj_delete(root);
        lv_free(avatar);
        return NULL;
    }
    lv_obj_remove_style_all(avatar->ball);
    lv_obj_remove_flag(avatar->ball, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(avatar->ball, size, size);
    lv_obj_center(avatar->ball);
    lv_obj_add_event_cb(avatar->ball, avatar_on_draw, LV_EVENT_DRAW_MAIN, avatar);

    ui_widget_init(&avatar->base, root, UI_WIDGET_TYPE_AVATAR);
    avatar->size = size;
    avatar->brightness = LV_OPA_COVER;
    avatar->state = AVATAR_STATE_NORMAL;
    avatar->motion_completion = NULL;
    avatar->motion_user_data = NULL;
    avatar->motion_start_x = 0;
    avatar->motion_progress = 1000;
    avatar->exiting = false;
    lv_obj_set_user_data(root, avatar);
    lv_obj_add_event_cb(root, avatar_on_delete, LV_EVENT_DELETE, avatar);
    return avatar;
}

void avatar_play_entrance(avatar_t* avatar,
                          avatar_entrance_complete_cb_t completion,
                          void* user_data) {
    lv_area_t area;

    if (!ui_widget_is_valid(UI_WIDGET(avatar))) {
        return;
    }
    lv_anim_t* current = lv_anim_get(avatar, avatar_animate_roll);
    int32_t start = current != NULL ? avatar->motion_progress : 0;
    if (current != NULL) {
        avatar_trace_roll_end(avatar, "replaced");
    }
    lv_anim_delete(avatar, avatar_animate_roll);
    lv_obj_update_layout(avatar->base.obj);
    lv_obj_get_coords(avatar->base.obj, &area);
    avatar->motion_start_x = lv_obj_get_style_translate_x(avatar->base.obj, LV_PART_MAIN) - (area.x2 + 1);
    avatar->motion_completion = completion;
    avatar->motion_user_data = user_data;
    avatar->exiting = false;
    avatar_apply_state(avatar);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, avatar);
    lv_anim_set_exec_cb(&animation, avatar_animate_roll);
    lv_anim_set_values(&animation, start, 1000);
    lv_anim_set_duration(&animation, AVATAR_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, avatar_roll_complete);
    avatar_trace_roll_start(avatar, AVATAR_ENTRANCE_DURATION_MS);
    if (lv_anim_start(&animation) == NULL) {
        avatar_trace_roll_end(avatar, "start_failed");
    }
}

bool avatar_play_exit(avatar_t* avatar,
                      avatar_animation_complete_cb_t completion,
                      void* user_data) {
    lv_area_t area;

    if (!ui_widget_is_valid(UI_WIDGET(avatar))) {
        return false;
    }
    if (avatar->exiting) {
        return true;
    }

    lv_anim_t* current = lv_anim_get(avatar, avatar_animate_roll);
    int32_t start = current != NULL ? avatar->motion_progress : 1000;
    if (current != NULL) {
        avatar_trace_roll_end(avatar, "replaced");
    }
    lv_anim_delete(avatar, avatar_animate_roll);
    lv_obj_update_layout(avatar->base.obj);
    lv_obj_get_coords(avatar->base.obj, &area);
    avatar->motion_start_x = lv_obj_get_style_translate_x(avatar->base.obj, LV_PART_MAIN) - (area.x2 + 1);
    avatar->motion_completion = completion;
    avatar->motion_user_data = user_data;
    avatar->exiting = true;
    avatar_apply_state(avatar);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, avatar);
    lv_anim_set_exec_cb(&animation, avatar_animate_roll);
    lv_anim_set_values(&animation, start, 0);
    lv_anim_set_duration(&animation, LV_MAX(1, AVATAR_EXIT_DURATION_MS * start / 1000));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&animation, avatar_roll_complete);
    avatar_trace_roll_start(avatar, LV_MAX(1, AVATAR_EXIT_DURATION_MS * start / 1000));
    if (lv_anim_start(&animation) == NULL) {
        avatar_trace_roll_end(avatar, "start_failed");
        avatar->motion_completion = NULL;
        avatar->motion_user_data = NULL;
        avatar->exiting = false;
        avatar_apply_state(avatar);
        return false;
    }
    return true;
}

void avatar_set_state(avatar_t* avatar, avatar_state_t state) {
    if (!ui_widget_is_valid(UI_WIDGET(avatar)) ||
        (state != AVATAR_STATE_NORMAL && state != AVATAR_STATE_LISTENING) ||
        avatar->state == state) {
        return;
    }
    avatar->state = state;
    avatar_apply_state(avatar);
}

void avatar_set_visible(avatar_t* avatar, bool visible) {
    if (!ui_widget_is_valid(UI_WIDGET(avatar)) ||
        ui_widget_is_hidden(UI_WIDGET(avatar)) == !visible) {
        return;
    }
    if (!visible) {
        if (lv_anim_get(avatar, avatar_animate_roll) != NULL) {
            avatar_trace_roll_end(avatar, "cancelled");
        }
        lv_anim_delete(avatar, avatar_animate_roll);
        avatar->motion_completion = NULL;
        avatar->motion_user_data = NULL;
        avatar->exiting = false;
    }
    ui_widget_set_visible(UI_WIDGET(avatar), visible);
    avatar_apply_state(avatar);
}
