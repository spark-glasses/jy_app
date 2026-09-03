#ifndef COMMON_WIDGETS_AVATAR_H
#define COMMON_WIDGETS_AVATAR_H

#include "ui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct avatar_t avatar_t;
typedef void (*avatar_animation_complete_cb_t)(avatar_t* avatar, void* user_data);
typedef avatar_animation_complete_cb_t avatar_entrance_complete_cb_t;

typedef enum {
    AVATAR_STATE_NORMAL = 0,
    AVATAR_STATE_LISTENING,
} avatar_state_t;

/** Create a dithered avatar drawn in one LVGL task, without image buffers. */
avatar_t* avatar_create(lv_obj_t* parent, int32_t size);

/** Roll in from the left without changing the visual state. */
void avatar_play_entrance(avatar_t* avatar,
                          avatar_entrance_complete_cb_t completion,
                          void* user_data);

/**
 * Roll out to the left and stop the listening pulse. The widget is not deleted.
 * Completion runs once at the end and may delete the widget.
 * Repeating exit keeps the current animation and callback.
 * Entrance or deletion cancels exit without calling its completion.
 * Returns false if the widget is invalid or the animation cannot start.
 */
bool avatar_play_exit(avatar_t* avatar,
                      avatar_animation_complete_cb_t completion,
                      void* user_data);

/** Listening pulses size and brightness. Repeating the current state preserves the animation. */
void avatar_set_state(avatar_t* avatar, avatar_state_t state);

/** Hide cancels motion and pulsing without a completion callback. State is retained. */
void avatar_set_visible(avatar_t* avatar, bool visible);

/* Use UI_WIDGET(avatar) for layout and deletion. Parent deletion also cleans up. */

#ifdef __cplusplus
}
#endif

#endif
