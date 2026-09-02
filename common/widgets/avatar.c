#include "avatar.h"

#define AVATAR_LISTENING_MIN_PERCENT 80
#define AVATAR_LISTENING_HALF_CYCLE_MS 650
#define AVATAR_ENTRANCE_DURATION_MS 450
#define AVATAR_EXIT_DURATION_MS 300
#define AVATAR_ENTRANCE_TURN 3600
#define AVATAR_CHECKER_COUNT 5

struct avatar_t {
    ui_widget_t base;
    lv_obj_t* texture;
    avatar_state_t state;
    avatar_animation_complete_cb_t motion_completion;
    void* motion_user_data;
    int32_t motion_start_x;
    int32_t motion_progress;
    bool exiting;
};

static void avatar_animate_scale(void* var, int32_t value) {
    avatar_t* avatar = var;
    if (lv_obj_is_visible(avatar->base.obj)) {
        lv_obj_set_style_transform_scale(avatar->texture,
                                         LV_SCALE_NONE * value / 100,
                                         LV_PART_MAIN);
    }
}

static void avatar_animate_roll(void* var, int32_t value) {
    avatar_t* avatar = var;
    int32_t remaining = 1000 - value;

    avatar->motion_progress = value;
    lv_obj_set_style_translate_x(avatar->base.obj,
                                 avatar->motion_start_x * remaining / 1000,
                                 LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(avatar->texture,
                                        -AVATAR_ENTRANCE_TURN * remaining / 1000,
                                        LV_PART_MAIN);
}

static void avatar_roll_complete(lv_anim_t* animation) {
    avatar_t* avatar = animation->var;
    avatar_animation_complete_cb_t completion = avatar->motion_completion;
    void* user_data = avatar->motion_user_data;

    avatar->motion_completion = NULL;
    avatar->motion_user_data = NULL;
    if (completion != NULL) {
        completion(avatar, user_data);
    }
}

static void avatar_apply_state(avatar_t* avatar) {
    lv_anim_delete(avatar, avatar_animate_scale);
    lv_obj_set_style_transform_scale(avatar->texture, LV_SCALE_NONE, LV_PART_MAIN);

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

    lv_anim_delete(avatar, avatar_animate_scale);
    lv_anim_delete(avatar, avatar_animate_roll);
    lv_obj_set_user_data(avatar->base.obj, NULL);
    lv_free(avatar);
}

static bool avatar_create_texture(avatar_t* avatar, lv_obj_t* root, int32_t size) {
    int32_t tile_size = LV_MAX(size / 6, 1);
    int32_t start = (size - tile_size * AVATAR_CHECKER_COUNT) / 2;

    avatar->texture = lv_obj_create(root);
    if (avatar->texture == NULL) {
        return false;
    }
    lv_obj_remove_style_all(avatar->texture);
    lv_obj_remove_flag(avatar->texture, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(avatar->texture, size, size);
    lv_obj_set_style_radius(avatar->texture, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(avatar->texture, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(avatar->texture, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(avatar->texture, lv_color_hex(0x789C58), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(avatar->texture, lv_color_hex(0x456B38), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(avatar->texture, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(avatar->texture, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(avatar->texture, lv_color_hex(0xA4C87A), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(avatar->texture, size / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(avatar->texture, size / 2, LV_PART_MAIN);
    lv_obj_center(avatar->texture);

    for (int32_t row = 0; row < AVATAR_CHECKER_COUNT; row++) {
        for (int32_t column = 0; column < AVATAR_CHECKER_COUNT; column++) {
            if ((row + column) % 2 != 0) {
                continue;
            }
            lv_obj_t* tile = lv_obj_create(avatar->texture);
            if (tile == NULL) {
                return false;
            }
            lv_obj_remove_style_all(tile);
            lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(tile, tile_size, tile_size);
            lv_obj_set_pos(tile, start + column * tile_size, start + row * tile_size);
            lv_obj_set_style_radius(tile, 1, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(tile, lv_color_hex(0xA5CC76), LV_PART_MAIN);
        }
    }
    return true;
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

    avatar_t* avatar = lv_malloc(sizeof(*avatar));
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
    lv_anim_start(&animation);
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
    if (lv_anim_start(&animation) == NULL) {
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
