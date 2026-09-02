#include "avatar.h"
#include "floatair_dbg.h"

#define AVATAR_LISTENING_MIN_PERCENT 80
#define AVATAR_LISTENING_HALF_CYCLE_MS 650
#define AVATAR_ENTRANCE_DURATION_MS 450
#define AVATAR_EXIT_DURATION_MS 300
#define AVATAR_ENTRANCE_TURN 3600
#define AVATAR_CHECKER_COUNT 6   /* tiles across the visible hemisphere */
#define AVATAR_EDGE_AA_PX 1.0f
#define AVATAR_RIM_PX 1.5f

/* On the 8-bit hardware display these reduce to brightness levels. */
#define AVATAR_COLOR_BASE 0x4B6B34u
#define AVATAR_COLOR_TILE 0xB9DC90u
#define AVATAR_COLOR_RIM  0xC6E49Eu
#define AVATAR_SHADE_MAX  215u   /* shadow alpha on the side facing away from the light */

/*
 * The ball is two pre-rendered ARGB8888 images, generated once at creation:
 *  - ball:  a checkered sphere, rotated by the roll animation
 *  - shade: a fixed lighting overlay so the light stays put while the ball rolls
 * Each frame is therefore two image blits (one transformed) instead of a
 * rotated, corner-clipped container with a dozen children, which needs an
 * intermediate layer every frame.
 */
struct avatar_t {
    ui_widget_t base;
    lv_obj_t* ball;
    lv_obj_t* shade;
    lv_draw_buf_t* ball_buf;
    lv_draw_buf_t* shade_buf;
    int32_t size;
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

/* Integer square root; keeps the texture generator free of libm. */
static uint32_t avatar_isqrt(uint32_t v) {
    uint32_t r = 0;
    uint32_t bit = 1u << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* sqrt(x) for x in [0, 1]. */
static float avatar_sqrt_unit(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    /* Q16 in, Q16 out: sqrt(x * 2^32) = sqrt(x) * 2^16 */
    return (float)avatar_isqrt((uint32_t)(x * 65536.0f) << 16) / 65536.0f;
}

/* atan(t) for |t| <= 1, max error ~0.005 rad. */
static float avatar_atan_unit(float t) {
    float a = t < 0.0f ? -t : t;
    return t * (0.7853982f + 0.273f * (1.0f - a));
}

static float avatar_atan(float t) {
    if (t > 1.0f) {
        return 1.5707963f - avatar_atan_unit(1.0f / t);
    }
    if (t < -1.0f) {
        return -1.5707963f - avatar_atan_unit(1.0f / t);
    }
    return avatar_atan_unit(t);
}

static void avatar_render_textures(lv_draw_buf_t* ball, lv_draw_buf_t* shade, int32_t size) {
    const float radius = (float)size * 0.5f;
    const float tile_angle = 3.1415927f / AVATAR_CHECKER_COUNT;
    /* Light from the upper left, toward the viewer. Roughly unit length. */
    const float lx = -0.55f;
    const float ly = -0.45f;
    const float lz = 0.70f;

    for (int32_t y = 0; y < size; y++) {
        uint32_t* ball_row = (uint32_t*)(ball->data + (uint32_t)y * ball->header.stride);
        uint32_t* shade_row = (uint32_t*)(shade->data + (uint32_t)y * shade->header.stride);
        for (int32_t x = 0; x < size; x++) {
            /* Unit-sphere coordinates of this pixel centre. */
            float nx = ((float)x + 0.5f - radius) / radius;
            float ny = ((float)y + 0.5f - radius) / radius;
            float d2 = nx * nx + ny * ny;
            /* Distance inside the rim in pixels, good enough near the edge. */
            float edge = (1.0f - d2) * radius * 0.5f;
            uint32_t cov;

            if (edge <= 0.0f) {
                ball_row[x] = 0;
                shade_row[x] = 0;
                continue;
            }
            cov = edge >= AVATAR_EDGE_AA_PX ? 255u : (uint32_t)(edge / AVATAR_EDGE_AA_PX * 255.0f);

            float nz = avatar_sqrt_unit(1.0f - d2);
            float depth = nz < 0.02f ? 0.02f : nz;
            float lon = avatar_atan(nx / depth);
            float lat_base = avatar_sqrt_unit(1.0f - ny * ny);
            float lat = avatar_atan(ny / (lat_base < 0.02f ? 0.02f : lat_base));
            int32_t lon_i = (int32_t)(lon / tile_angle + 64.0f);
            int32_t lat_i = (int32_t)(lat / tile_angle + 64.0f);
            bool light_tile = ((lon_i + lat_i) & 1) != 0;
            uint32_t color = edge < AVATAR_RIM_PX ? AVATAR_COLOR_RIM
                             : light_tile          ? AVATAR_COLOR_TILE
                                                   : AVATAR_COLOR_BASE;
            ball_row[x] = (cov << 24) | color;

            float lambert = nx * lx + ny * ly + nz * lz;
            if (lambert < 0.0f) {
                lambert = 0.0f;
            }
            float dark = 1.0f - lambert;
            dark = dark * dark; /* keep the lit side clean, darken past the terminator */
            uint32_t shade_a = (uint32_t)(dark * (float)AVATAR_SHADE_MAX) * cov / 255u;
            shade_row[x] = shade_a << 24; /* black */
        }
    }
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

static void avatar_set_zoom(avatar_t* avatar, uint32_t zoom) {
    lv_image_set_scale(avatar->ball, zoom);
    lv_image_set_scale(avatar->shade, zoom);
}

static void avatar_animate_scale(void* var, int32_t value) {
    avatar_t* avatar = var;
    if (lv_obj_is_visible(avatar->base.obj)) {
        avatar_set_zoom(avatar, LV_SCALE_NONE * value / 100);
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
                                 avatar->motion_start_x * remaining / 1000,
                                 LV_PART_MAIN);
    /* Only the checker rotates; the lighting overlay stays fixed. */
    lv_image_set_rotation(avatar->ball, -AVATAR_ENTRANCE_TURN * remaining / 1000);
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
    lv_anim_delete(avatar, avatar_animate_scale);
    avatar_set_zoom(avatar, LV_SCALE_NONE);

    if (avatar->exiting || avatar->state != AVATAR_STATE_LISTENING ||
        ui_widget_is_hidden(UI_WIDGET(avatar))) {
        return;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, avatar);
    lv_anim_set_exec_cb(&animation, avatar_animate_scale);
    lv_anim_set_values(&animation, 100, AVATAR_LISTENING_MIN_PERCENT);
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
    lv_anim_delete(avatar, avatar_animate_scale);
    lv_anim_delete(avatar, avatar_animate_roll);
    /* Children are deleted after this event; detach the buffers first. */
    if (avatar->ball != NULL) {
        lv_image_set_src(avatar->ball, NULL);
    }
    if (avatar->shade != NULL) {
        lv_image_set_src(avatar->shade, NULL);
    }
    if (avatar->ball_buf != NULL) {
        lv_draw_buf_destroy(avatar->ball_buf);
    }
    if (avatar->shade_buf != NULL) {
        lv_draw_buf_destroy(avatar->shade_buf);
    }
    lv_obj_set_user_data(avatar->base.obj, NULL);
    lv_free(avatar);
}

static lv_obj_t* avatar_create_layer(lv_obj_t* root, lv_draw_buf_t* buf, int32_t size) {
    lv_obj_t* img = lv_image_create(root);
    if (img == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(img);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(img, size, size);
    lv_image_set_src(img, buf);
    lv_image_set_pivot(img, size / 2, size / 2);
    lv_image_set_antialias(img, true);
    lv_obj_center(img);
    return img;
}

static bool avatar_create_texture(avatar_t* avatar, lv_obj_t* root, int32_t size) {
    avatar->size = size;
    avatar->ball_buf = lv_draw_buf_create((uint32_t)size, (uint32_t)size, LV_COLOR_FORMAT_ARGB8888, 0);
    avatar->shade_buf = lv_draw_buf_create((uint32_t)size, (uint32_t)size, LV_COLOR_FORMAT_ARGB8888, 0);
    if (avatar->ball_buf == NULL || avatar->shade_buf == NULL) {
        return false;
    }
    avatar_render_textures(avatar->ball_buf, avatar->shade_buf, size);

    avatar->ball = avatar_create_layer(root, avatar->ball_buf, size);
    avatar->shade = avatar_create_layer(root, avatar->shade_buf, size);
    return avatar->ball != NULL && avatar->shade != NULL;
}

avatar_t* avatar_create(lv_obj_t* parent, int32_t size) {
    if (size < AVATAR_CHECKER_COUNT) {
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
    lv_obj_set_size(root, size, size);

    if (!avatar_create_texture(avatar, root, size)) {
        if (avatar->ball_buf != NULL) {
            lv_draw_buf_destroy(avatar->ball_buf);
        }
        if (avatar->shade_buf != NULL) {
            lv_draw_buf_destroy(avatar->shade_buf);
        }
        lv_obj_delete(root);
        lv_free(avatar);
        return NULL;
    }

    ui_widget_init(&avatar->base, root, UI_WIDGET_TYPE_AVATAR);
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
    ui_widget_set_visible(UI_WIDGET(avatar), visible);
    avatar_apply_state(avatar);
}
