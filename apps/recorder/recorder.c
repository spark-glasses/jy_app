/**
 * @file recorder.c
 * @brief 录音应用生命周期、消息注册与状态栏上方状态视图实现。
 */
#include "recorder.h"

#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_nav.h"
#include "common/app_framework/app_router.h"
#include "common/widgets/container.h"
#include "common/widgets/label.h"
#include "floatair_dbg.h"
#include "message.h"
#include "product_app.h"
#include "system/system_runtime_ui.h"

#include <lvgl/lvgl.h>
#include <stdio.h>

#define RECORDER_INDICATOR_SIZE   20  ///< 圆点和三角状态图标的宽高。
#define RECORDER_INDICATOR_GAP    12  ///< 计时文本与状态图标之间的间距。
#define RECORDER_STATUS_HEIGHT    24  ///< 计时和状态图标组合的高度。
#define RECORDER_STATUS_BOTTOM_GAP 8  ///< 状态组合距离底部状态栏上边缘的距离。

static label_t* s_recorder_time_label = NULL; ///< 录音计时文本。
static lv_obj_t* s_recorder_dot = NULL;       ///< Running 状态圆点。
static lv_obj_t* s_recorder_triangle = NULL;  ///< Pause 状态三角图标。
static recorder_state_t s_recorder_state = RECORDER_STATE_PAUSE; ///< 当前录音状态。
static uint32_t s_recorder_tick_seconds = 0;  ///< 当前录音计时秒数。

static app_message_t s_recorder_msg = {
    .id = APP_MSG_ID_RECORDER,
    .name = APP_NAME_RECORDER,
    .cb = recorder_route_cmd,
};

/**
 * @brief 绘制白色实心三角图标。
 * @param[in] event LVGL 绘制事件。
 * @return 无返回值。
 */
static void recorder_triangle_draw(lv_event_t* event) {
    lv_obj_t* triangle = lv_event_get_target(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_draw_triangle_dsc_t draw_dsc;

    if (triangle == NULL || layer == NULL) {
        return;
    }

    lv_obj_get_coords(triangle, &coords);
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.bg_color = lv_color_white();
    draw_dsc.bg_opa = LV_OPA_COVER;
    draw_dsc.p[0].x = coords.x1;
    draw_dsc.p[0].y = coords.y1;
    draw_dsc.p[1].x = coords.x1;
    draw_dsc.p[1].y = coords.y2;
    draw_dsc.p[2].x = coords.x2;
    draw_dsc.p[2].y = coords.y1 + (lv_area_get_height(&coords) / 2);
    lv_draw_triangle(layer, &draw_dsc);
}

/**
 * @brief 将秒数格式化并刷新为 Prompter 同款 HH:MM:SS 文本。
 * @param[in] tick_seconds 录音计时秒数。
 * @return 无返回值。
 */
static void recorder_update_time_label(uint32_t tick_seconds) {
    uint32_t hours = tick_seconds / 3600U;
    uint32_t minutes = (tick_seconds / 60U) % 60U;
    uint32_t seconds = tick_seconds % 60U;
    char text[16] = {0};

    if (s_recorder_time_label == NULL) {
        return;
    }

    snprintf(text,
             sizeof(text),
             "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
    label_set_text(s_recorder_time_label, text);
}

/**
 * @brief 按当前状态和计时同步圆点、三角图标。
 * @return 无返回值。
 */
static void recorder_sync_indicator(void) {
    bool running = s_recorder_state == RECORDER_STATE_RUNNING;
    bool dot_visible = running && ((s_recorder_tick_seconds % 2U) == 0U);

    if (s_recorder_dot != NULL) {
        lv_obj_set_style_bg_opa(s_recorder_dot,
                                dot_visible ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
        if (running) {
            lv_obj_remove_flag(s_recorder_dot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_recorder_dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_recorder_triangle != NULL) {
        if (running) {
            lv_obj_add_flag(s_recorder_triangle, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_recorder_triangle, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 设置录音状态并刷新状态图标。
 * @param[in] state Pause 或 Running 状态。
 * @return 无返回值。
 */
void recorder_set_state(recorder_state_t state) {
    s_recorder_state = state;
    if (state == RECORDER_STATE_RUNNING) {
        s_recorder_tick_seconds = 0;
        recorder_update_time_label(0);
    }
    recorder_sync_indicator();
}

/**
 * @brief 更新录音计时并驱动 Running 圆点亮灭。
 * @param[in] tick_seconds 录音计时秒数。
 * @return 无返回值。
 */
void recorder_set_tick(uint32_t tick_seconds) {
    s_recorder_tick_seconds = tick_seconds;
    recorder_update_time_label(tick_seconds);
    recorder_sync_indicator();
}

/**
 * @brief 处理录音页面的退出手势。
 * @param[in] event LVGL 事件对象。
 * @return 无返回值。
 */
static void recorder_touch_event_handle(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_DCLICKED) {
        (void)app_router_exit_current_app();
    }
}

/**
 * @brief 创建录音状态视图。
 * @param[in] root 页面根对象。
 * @param[in] data 页面入参，当前未使用。
 * @return 无返回值。
 */
static void recorder_page_create(lv_obj_t* root, const app_page_data_t* data) {
    container_cfg_t status_cfg = container_default_cfg();
    label_cfg_t time_cfg = label_default_cfg();
    container_t* status_group = NULL;
    lv_obj_t* status_obj = NULL;
    lv_obj_t* indicator_box = NULL;

    (void)data;
    floatair_assert(root != NULL, "recorder root NULL");

    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    status_cfg.w = LV_SIZE_CONTENT;
    status_cfg.h = RECORDER_STATUS_HEIGHT;
    status_cfg.pad_hor = 0;
    status_cfg.pad_ver = 0;
    status_cfg.border_width = 0;
    status_cfg.opa = LV_OPA_TRANSP;
    status_group = container_create(root, &status_cfg);
    floatair_assert(status_group != NULL, "recorder status group create failed");
    container_set_layout_hbox_spaced(status_group, RECORDER_INDICATOR_GAP);
    container_set_align(status_group,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    status_obj = container_get_obj(status_group);
    lv_obj_align(status_obj, LV_ALIGN_BOTTOM_MID, 0, -RECORDER_STATUS_BOTTOM_GAP);

    time_cfg.w = LV_SIZE_CONTENT;
    time_cfg.h = LV_SIZE_CONTENT;
    time_cfg.font.weight = 20;
    time_cfg.align = LABEL_ALIGN_CENTER;
    time_cfg.overflow = LABEL_OVERFLOW_CLIP;
    time_cfg.text = "00:00:00";
    s_recorder_time_label = label_create(status_obj, &time_cfg);
    floatair_assert(s_recorder_time_label != NULL, "recorder time label create failed");

    indicator_box = lv_obj_create(status_obj);
    floatair_assert(indicator_box != NULL, "recorder indicator box create failed");
    lv_obj_remove_style_all(indicator_box);
    lv_obj_set_size(indicator_box, RECORDER_INDICATOR_SIZE, RECORDER_INDICATOR_SIZE);
    lv_obj_remove_flag(indicator_box, LV_OBJ_FLAG_CLICKABLE);

    s_recorder_dot = lv_obj_create(indicator_box);
    floatair_assert(s_recorder_dot != NULL, "recorder dot create failed");
    lv_obj_remove_style_all(s_recorder_dot);
    lv_obj_set_size(s_recorder_dot, RECORDER_INDICATOR_SIZE, RECORDER_INDICATOR_SIZE);
    lv_obj_center(s_recorder_dot);
    lv_obj_set_style_radius(s_recorder_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_recorder_dot, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_recorder_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_recorder_dot, LV_OBJ_FLAG_CLICKABLE);

    s_recorder_triangle = lv_obj_create(indicator_box);
    floatair_assert(s_recorder_triangle != NULL, "recorder triangle create failed");
    lv_obj_remove_style_all(s_recorder_triangle);
    lv_obj_set_size(s_recorder_triangle, RECORDER_INDICATOR_SIZE, RECORDER_INDICATOR_SIZE);
    lv_obj_center(s_recorder_triangle);
    lv_obj_add_event_cb(s_recorder_triangle, recorder_triangle_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_remove_flag(s_recorder_triangle, LV_OBJ_FLAG_CLICKABLE);

    s_recorder_state = RECORDER_STATE_PAUSE;
    s_recorder_tick_seconds = 0;
    recorder_update_time_label(0);
    recorder_sync_indicator();
}

/**
 * @brief 显示录音页面并启用系统状态栏。
 * @param[in] root 页面根对象。
 * @return 无返回值。
 */
static void recorder_page_appear(lv_obj_t* root) {
    floatair_assert(root != NULL, "recorder root NULL");

    system_status_bar_set_mode(true);
    lv_obj_add_event_cb(root, recorder_touch_event_handle, LV_EVENT_DCLICKED, NULL);
}

/**
 * @brief 清理录音页面对象引用。
 * @return 无返回值。
 */
static void recorder_page_destroy(void) {
    s_recorder_time_label = NULL;
    s_recorder_dot = NULL;
    s_recorder_triangle = NULL;
}

static app_page_t s_recorder_page = {
    .name = APP_NAME_RECORDER,
    .on_create = recorder_page_create,
    .on_appear = recorder_page_appear,
    .on_disappear = NULL,
    .on_destroy = recorder_page_destroy,
    .on_unload = NULL,
    .on_back = NULL,
};

/**
 * @brief 启动录音应用并展示状态页面。
 * @return 无返回值。
 */
static void recorder_app_on_start(void) {
    if (!app_nav_replace(&s_recorder_page, NULL, 0)) {
        floatair_assert(false, "recorder page replace failed");
    }
}

static app_t s_recorder_app = {
    .name = APP_NAME_RECORDER,
    .on_start = recorder_app_on_start,
    .on_resume = NULL,
    .on_pause = NULL,
    .on_stop = NULL,
    .on_back = NULL,
};

bool recorder_app_register(void) {
    if (app_msg_register(&s_recorder_msg) != 0) {
        return false;
    }
    return app_manager_register(&s_recorder_app);
}
