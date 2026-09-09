/**
 * @file system_runtime_ui.c
 * @brief 系统运行时 UI 壳层实现
 * @author jytek
 * @version 1.0.0
 * @date 2026-04-16
 * @copyright JYTek
 * @ingroup app_system
 */
#include "system/system_runtime_ui.h"

#include "system/system.h"
#include "system/system_config_json.h"
#include "system/system_notification.h"
#include "system/system_res.h"
#include "system_bt_disconnect_overlay_ui.h"
#include "floatair_lcd.h"
#include "common/app_framework/app_manager.h"
#include "common/app_framework/app_layers.h"
#include "common/app_framework/app_router.h"
#include "common/app_framework/app_stereo.h"
#include "system/popups/notify/notify.h"
#include "common/widgets/toast.h"
#include "common/widgets/avatar.h"
#include "common/widgets/label.h"
#include "system/popups/notify_list/notify_list.h"
#include "app_lcd.h"
#include "lvgl/src/font/lv_binfont_loader.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>

#define SYSTEM_FOOTER_HEIGHT 72
#define SYSTEM_AVATAR_LEFT 35
#define SYSTEM_AVATAR_SIZE 40
#define SYSTEM_AVATAR_BOTTOM 16
#define SYSTEM_REPLY_GAP 16
#define SYSTEM_REPLY_LEFT (SYSTEM_AVATAR_LEFT + SYSTEM_AVATAR_SIZE + SYSTEM_REPLY_GAP)
#define SYSTEM_REPLY_RIGHT 24
#define SYSTEM_REPLY_EDGE_PADDING 8
#define SYSTEM_REPLY_PADDING_HOR 12
#define SYSTEM_REPLY_PADDING_VER 8
#define SYSTEM_REPLY_BORDER_WIDTH 1
#define SYSTEM_REPLY_RADIUS 12
#define SYSTEM_FRAME_PERIOD_MS 67
#define SYSTEM_REPLY_FONT_SIZE 12
#define SYSTEM_REPLY_LINE_SPACE 2
#define SYSTEM_REPLY_FONT_PATH "A:/romfs/system/font/open_runde_12.bin"

static lv_obj_t* g_status_bar_top = NULL;          ///< app 层顶部状态栏对象
static lv_obj_t* g_page_content = NULL;           ///< Page area between the header and footer.
static lv_obj_t* g_footer = NULL;                 ///< System-owned footer outside the page roots.
static avatar_t* g_footer_avatar = NULL;
static lv_obj_t* g_reply_viewport = NULL;        ///< Reply bubble; a footer sibling that overlays the page.
static lv_obj_t* g_reply_label = NULL;
static lv_font_t* g_reply_bitmap_font = NULL;
static lv_obj_t* g_bt_disconnect_status_bar = NULL;   ///< 蓝牙断连遮罩层顶部状态栏对象
static bool g_status_bar_top_visible = true;       ///< 顶部状态栏期望显隐状态
static lv_obj_t* g_bt_disconnect_overlay = NULL;      ///< 蓝牙断连全屏遮罩
static system_bt_disconnect_overlay_ui_t g_bt_disconnect_overlay_ui; ///< 蓝牙断连遮罩生成布局句柄
static bool g_bt_disconnect_overlay_visible = false;  ///< 蓝牙断连遮罩当前显隐状态
static uint32_t g_display_level_applied = UINT32_MAX; ///< 最近一次已应用的显示距离档位
static uint8_t g_status_bar_battery = DEFAULT_BATTERY_LEVEL; ///< 最近一次通过消息推送同步的电量
static uint8_t g_status_bar_charge_state = 0;         ///< 最近一次通过消息推送同步的充电状态
static bool g_status_bar_wear_detection_visible = false; ///< 最近一次同步的佩戴检测图标显隐状态
static time_t g_status_bar_time_epoch = 0;            ///< 最近一次收到的设备时间戳
static bool g_status_bar_time_valid = false;          ///< 设备时间戳缓存是否有效
static bool g_status_bar_time_reliable = false;       ///< 时间是否已通过手机对表确认可靠
static bool g_status_bar_refresh_pending = false;     ///< 灭屏期间状态栏缓存变化后待亮屏刷新标记
static bool g_screen_refresh_pending = false;         ///< 灭屏期间收到强制刷屏请求后的待刷新标记
static uint32_t g_progress_hint_event_id = 0;         ///< 自定义进度提示事件 ID

static void system_ui_sync_app_layer_scene(void);

uint32_t system_ui_get_progress_hint_event(void) {
    if (g_progress_hint_event_id == 0) {
        g_progress_hint_event_id = lv_event_register_id();
    }

    return g_progress_hint_event_id;
}

bool system_ui_send_progress_hint(const system_progress_hint_param_t* param) {
    lv_obj_t* root = NULL;

    if (param == NULL) {
        return false;
    }

    root = app_manager_current_content_root();
    if (root == NULL || !lv_obj_is_valid(root)) {
        floatair_err("current page root is NULL");
        return false;
    }

    (void)lv_obj_send_event(root, system_ui_get_progress_hint_event(), (void*)param);
    return true;
}

/**
 * @brief 判断蓝牙断连遮罩是否应按连接态显示。
 * @return `true` 表示允许显示断连遮罩，`false` 表示当前应让前置流程独占页面。
 */
static bool system_bt_disconnect_overlay_should_show(void) {
    // The setup screen is needed only before the phone supplies app configuration.
    // A configured pair remains usable when its phone transport is disconnected.
    return !app_router_has_app_config();
}

/**
 * @brief 刷新蓝牙断连遮罩的提示文案与蓝牙名称。
 * @return 无返回值。
 */
void system_ui_refresh_bt_disconnect_overlay_text(void) {
    const char* bt_name = system_get_btname();
    lv_obj_t* notice_obj = NULL;
    lv_obj_t* name_obj = NULL;

    notice_obj = label_get_obj(g_bt_disconnect_overlay_ui.notice);
    name_obj = label_get_obj(g_bt_disconnect_overlay_ui.name);

    if (notice_obj == NULL || name_obj == NULL) {
        return;
    }

    label_set_text(g_bt_disconnect_overlay_ui.notice, app_get_str("IDLE_NOTICE_UNBOND"));
    label_set_text(g_bt_disconnect_overlay_ui.name, bt_name != NULL ? bt_name : "");
    lv_obj_update_layout(notice_obj);
    lv_obj_update_layout(name_obj);
}

/**
 * @brief 判断蓝牙断连遮罩当前是否处于激活状态。
 * @return `true` 表示遮罩有效且正在显示，`false` 表示遮罩未激活。
 */
static bool system_bt_disconnect_overlay_is_active(void) {
    return g_bt_disconnect_overlay_visible &&
           g_bt_disconnect_overlay != NULL &&
           lv_obj_is_valid(g_bt_disconnect_overlay);
}

/**
 * @brief 获取当前活动页面顶部状态栏对象。
 * @return 成功返回当前页面状态栏，失败返回 `NULL`。
 */
static lv_obj_t* system_ui_get_current_status_bar(void) {
    if (g_status_bar_top == NULL || !lv_obj_is_valid(g_status_bar_top)) {
        return NULL;
    }

    return g_status_bar_top;
}

/**
 * @brief Calculate page height between the optional header and permanent footer.
 * @param[in] show_top Whether the top status bar reserves space.
 * @return Page content height.
 */
static lv_coord_t system_ui_calc_page_content_height(bool show_top) {
    lv_coord_t height = (lv_coord_t)config_lcd.ui_height - SYSTEM_FOOTER_HEIGHT;

    if (show_top) {
        height -= STATUS_BAR_HEIGHT;
    }

    if (height < 0) {
        height = 0;
    }

    return height;
}

/**
 * @brief 获取当前状态栏模式下的页面内容区高度。
 * @return 返回内容区高度。
 */
lv_coord_t system_ui_get_page_content_height(void) {
    return system_ui_calc_page_content_height(g_status_bar_top_visible);
}

lv_obj_t* system_ui_get_page_parent(void) {
    return g_page_content;
}

lv_obj_t* system_ui_get_footer(void) {
    return g_footer;
}

void system_ui_set_avatar_visible(bool visible) {
    if (g_footer_avatar == NULL ||
        ui_widget_is_hidden(UI_WIDGET(g_footer_avatar)) == !visible) {
        return;
    }

    avatar_set_visible(g_footer_avatar, visible);
    if (visible) {
        avatar_play_entrance(g_footer_avatar, NULL, NULL);
    } else {
        avatar_set_state(g_footer_avatar, AVATAR_STATE_NORMAL);
    }
}

void system_ui_set_avatar_listening(bool listening) {
    if (g_footer_avatar != NULL) {
        avatar_set_state(g_footer_avatar,
                         listening ? AVATAR_STATE_LISTENING : AVATAR_STATE_NORMAL);
    }
}

void system_ui_sync_avatar_state(const char* source) {
    bool visible = system_runtime_state_get_display_on();
    bool listening = visible && system_get_btconn_state() &&
                     system_runtime_state_get_listen_state() == SYSTEM_LISTEN_STATE_LISTENING;

    system_ui_set_avatar_visible(visible);
    system_ui_set_avatar_listening(listening);
    floatair_info("avatar visible=%d listening=%d source=%s",
                  (int)visible, (int)listening, source != NULL ? source : "unknown");
}

/**
 * Size and place the reply bubble from one text measurement.
 *
 * The footer keeps its fixed height and the bubble, a sibling of the footer in
 * the app layer, overlays the page above it. A reply therefore invalidates only
 * the bubble's old and new areas. Resizing the page host instead made every
 * reply redraw the whole eye. The bubble is not a footer child because LVGL
 * clips invalidation to the parent's coords even with OVERFLOW_VISIBLE.
 */
static void system_ui_layout_footer(void) {
    if (g_reply_viewport == NULL || g_reply_label == NULL) return;

    const char* text = lv_label_get_text(g_reply_label);
    if (text == NULL || text[0] == '\0') {
        lv_obj_add_flag(g_reply_viewport, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const lv_font_t* font = lv_obj_get_style_text_font(g_reply_label, LV_PART_MAIN);
    lv_coord_t text_inset = SYSTEM_REPLY_PADDING_HOR + SYSTEM_REPLY_BORDER_WIDTH;
    lv_coord_t max_bubble_width = LV_MAX(1, (lv_coord_t)config_lcd.ui_width -
                                             SYSTEM_REPLY_LEFT - SYSTEM_REPLY_RIGHT);
    lv_coord_t max_text_width = LV_MAX(1, max_bubble_width - 2 * text_inset);
    lv_point_t size = {0, 0};

    /* Same wrapping as the label draw, without any object layout passes. */
    lv_text_get_size(&size, text, font, 0, SYSTEM_REPLY_LINE_SPACE, max_text_width, LV_TEXT_FLAG_NONE);
    lv_coord_t text_width = LV_CLAMP(1, (lv_coord_t)size.x, max_text_width);
    lv_coord_t text_height = LV_MAX(1, (lv_coord_t)size.y);

    lv_coord_t status_bar_bottom = g_status_bar_top_visible ? STATUS_BAR_HEIGHT : 0;
    lv_coord_t max_bubble_height = LV_MAX(1, (lv_coord_t)config_lcd.ui_height - status_bar_bottom -
                                              2 * SYSTEM_REPLY_EDGE_PADDING);
    lv_coord_t bubble_width = text_width + 2 * text_inset;
    lv_coord_t bubble_height = LV_MIN(text_height + 2 * (SYSTEM_REPLY_PADDING_VER + SYSTEM_REPLY_BORDER_WIDTH),
                                      max_bubble_height);

    /* Layer-local top: centered on the ball, kept below the status bar and
     * above the bottom edge padding. Anchored to the bottom like the footer. */
    lv_coord_t ui_height = (lv_coord_t)config_lcd.ui_height;
    lv_coord_t avatar_mid_y = ui_height - SYSTEM_AVATAR_BOTTOM - SYSTEM_AVATAR_SIZE / 2;
    lv_coord_t min_top = status_bar_bottom + SYSTEM_REPLY_EDGE_PADDING;
    lv_coord_t max_top = LV_MAX(min_top, ui_height - SYSTEM_REPLY_EDGE_PADDING - bubble_height);
    lv_coord_t top = LV_CLAMP(min_top, avatar_mid_y - bubble_height / 2, max_top);

    lv_obj_set_size(g_reply_label, text_width, text_height);
    lv_obj_set_size(g_reply_viewport, bubble_width, bubble_height);
    lv_obj_align(g_reply_viewport, LV_ALIGN_BOTTOM_LEFT, SYSTEM_REPLY_LEFT,
                 -(ui_height - top - bubble_height));
    lv_obj_remove_flag(g_reply_viewport, LV_OBJ_FLAG_HIDDEN);
}

static void system_ui_layout_page_content(void) {
    system_ui_layout_footer();
    if (g_page_content == NULL) {
        return;
    }
    lv_obj_set_size(g_page_content, config_lcd.ui_width, system_ui_get_page_content_height());
    lv_obj_align(g_page_content, LV_ALIGN_TOP_LEFT, 0,
                 g_status_bar_top_visible ? STATUS_BAR_HEIGHT : 0);
}

static const lv_font_t* system_ui_reply_font(void) {
    if (g_reply_bitmap_font == NULL) {
        g_reply_bitmap_font = lv_binfont_create(SYSTEM_REPLY_FONT_PATH);
        if (g_reply_bitmap_font != NULL) {
            g_reply_bitmap_font->fallback = get_font_by_size_near(SYSTEM_REPLY_FONT_SIZE);
        }
        floatair_info("reply font load path=%s size=%u loaded=%d",
                      SYSTEM_REPLY_FONT_PATH, (unsigned int)SYSTEM_REPLY_FONT_SIZE,
                      g_reply_bitmap_font != NULL);
    }
    return g_reply_bitmap_font != NULL
        ? g_reply_bitmap_font
        : get_font_by_size_near(SYSTEM_REPLY_FONT_SIZE);
}

bool system_ui_set_reply(const char* text) {
    if (text == NULL || g_reply_label == NULL || g_reply_viewport == NULL) return false;
    if (strcmp(lv_label_get_text(g_reply_label), text) == 0) return true;

    lv_label_set_text(g_reply_label, text);
    system_ui_layout_footer();
    lv_obj_scroll_to_y(g_reply_viewport, 0, LV_ANIM_OFF);
    system_ui_request_frame();
    return true;
}

bool system_ui_scroll_reply(lv_event_code_t code) {
    if ((code != LV_EVENT_GESTURE_LEFT && code != LV_EVENT_GESTURE_RIGHT) ||
        g_reply_viewport == NULL || g_reply_label == NULL ||
        lv_obj_has_flag(g_reply_viewport, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    lv_obj_update_layout(g_reply_viewport);
    lv_coord_t max_scroll = lv_obj_get_scroll_y(g_reply_viewport) +
                            lv_obj_get_scroll_bottom(g_reply_viewport);
    if (max_scroll <= 0) return false;

    /* Page by the visible height, keeping one line of context. */
    const lv_font_t* font = lv_obj_get_style_text_font(g_reply_label, LV_PART_MAIN);
    lv_coord_t line_advance = (lv_coord_t)lv_font_get_line_height(font) + SYSTEM_REPLY_LINE_SPACE;
    lv_coord_t step = LV_MAX(1, lv_obj_get_content_height(g_reply_viewport) - line_advance);
    lv_coord_t next = lv_obj_get_scroll_y(g_reply_viewport) +
                     (code == LV_EVENT_GESTURE_LEFT ? step : -step);
    lv_obj_scroll_to_y(g_reply_viewport, LV_CLAMP(0, next, max_scroll), LV_ANIM_OFF);
    return true;
}

/**
 * @brief 仅在 display level 变化时刷新底层 3D 根距离。
 * @return `true` 表示本次实际更新了根距离，`false` 表示当前档位未变化。
 */
static bool system_ui_apply_display_distance_level_if_needed(void) {
    uint32_t level = system_config_get_displaylevel();
    int32_t distance = system_get_displaylevel_value(level);

    if (g_display_level_applied == level) {
        return false;
    }

    floatair_info("apply display distance level: prev=%" PRIu32 " next=%" PRIu32 " distance=%" PRId32,
                  (uint32_t)g_display_level_applied,
                  (uint32_t)level,
                  distance);
    jyt_dual_screen_set_root_distance((int)distance);
    g_display_level_applied = level;
    system_ui_sync_app_layer_scene();
    return true;
}

/**
 * @brief 同步 app 层尺寸与显示距离位移。
 * @return 无返回值。
 */
static void system_ui_sync_app_layer_scene(void) {
    app_layers_resize((int32_t)config_lcd.ui_width, (int32_t)config_lcd.ui_height);
    system_ui_layout_page_content();
}

/**
 * @brief 使用指定时间戳刷新状态栏时间显示。
 * @param[in] status_bar 目标状态栏对象。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
static bool system_ui_refresh_status_bar_time(lv_obj_t* status_bar, time_t time_now) {
    char timebuf[8] = {0};
    struct tm* ptm = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        floatair_dbg("status bar time refresh skipped: lcd off");
        return true;
    }

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return true;
    }

    ptm = localtime(&time_now);
    if (ptm == NULL) {
        floatair_err("localtime failed");
        return false;
    }

    snprintf(timebuf, sizeof(timebuf), "%02d:%02d", ptm->tm_hour, ptm->tm_min);
    status_bar_update_time(status_bar, timebuf);
    return true;
}

/**
 * @brief 设置单个状态栏时间文本显隐。
 * @param[in] status_bar 目标状态栏对象。
 * @param[in] visible `true` 表示显示时间，`false` 表示隐藏时间。
 * @return 无返回值。
 */
static void system_ui_set_status_bar_time_visible(lv_obj_t* status_bar, bool visible) {
    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_set_time_visible(status_bar, visible);
}

/**
 * @brief 设置状态栏时间是否已通过手机对表确认可靠。
 * @param[in] reliable `true` 表示时间可靠并允许显示，`false` 表示隐藏时间。
 * @return 无返回值。
 */
void system_ui_set_time_reliable(bool reliable) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_time_reliable = reliable;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("time reliable ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    system_ui_set_status_bar_time_visible(status_bar, reliable);
    system_ui_set_status_bar_time_visible(g_bt_disconnect_status_bar, reliable);
}

/**
 * @brief 按当前遮罩状态同步当前页顶部状态栏显隐。
 * @return 无返回值。
 */
static void system_bt_disconnect_overlay_sync_status_bar(void) {
    lv_obj_t* status_bar = system_ui_get_current_status_bar();
    const char* current_app = app_manager_current_name();
    bool show_top = g_status_bar_top_visible;

    if (status_bar == NULL) {
        floatair_info("sync bt disconnect status bar skipped: status_bar=NULL, app=%s, current_app=%s, overlay_visible=%d, top_expected=%d",
                      app_router_get_app(),
                      (current_app != NULL) ? current_app : "N/A",
                      (int)g_bt_disconnect_overlay_visible,
                      (int)show_top);
        return;
    }

    status_bar_set_visible(status_bar,
                           g_bt_disconnect_overlay_visible ? false : show_top);
    floatair_info("sync bt disconnect status bar: status_bar=%p, current_app=%s, overlay_visible=%d, top_expected=%d",
                  status_bar,
                  (current_app != NULL) ? current_app : "N/A",
                  (int)g_bt_disconnect_overlay_visible,
                  (int)show_top);

    if (!g_bt_disconnect_overlay_visible && show_top) {
        lv_obj_move_foreground(status_bar);
    }
}

/**
 * @brief 创建 app 层顶部状态栏。
 * @param[in] parent 状态栏父对象。
 * @return 无返回值。
 */
static void system_status_bar_top_create(lv_obj_t* parent) {
    if (parent == NULL || g_status_bar_top != NULL) {
        return;
    }

    g_status_bar_top = status_bar_create_with_pos(
        parent,
        (int32_t)config_lcd.ui_width,
        NULL,
        STATUS_BAR_POS_TOP);
    floatair_assert(g_status_bar_top != NULL, "top status bar create failed");
    system_ui_refresh_status_bar(g_status_bar_top);
    status_bar_set_visible(g_status_bar_top, g_status_bar_top_visible);
    lv_obj_move_foreground(g_status_bar_top);
    floatair_info("top status bar created: parent=%p status_bar=%p", parent, g_status_bar_top);
}

static void system_footer_avatar_deleted(lv_event_t* event) {
    (void)event;
    g_footer_avatar = NULL;
}

/** Create the footer and its avatar once, alongside the top status bar. */
static void system_footer_create(lv_obj_t* parent) {
    if (parent == NULL || g_footer != NULL) {
        return;
    }

    g_footer = lv_obj_create(parent);
    floatair_assert(g_footer != NULL, "footer create failed");
    lv_obj_remove_style_all(g_footer);
    lv_obj_remove_flag(g_footer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE |
                                    LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_size(g_footer, LV_PCT(100), SYSTEM_FOOTER_HEIGHT);
    lv_obj_align(g_footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_null_on_delete(&g_footer);

    g_footer_avatar = avatar_create(g_footer, SYSTEM_AVATAR_SIZE);
    floatair_assert(g_footer_avatar != NULL, "footer avatar create failed");
    lv_obj_t* avatar_obj = ui_widget_get_obj(UI_WIDGET(g_footer_avatar));
    lv_obj_align(avatar_obj, LV_ALIGN_BOTTOM_LEFT, SYSTEM_AVATAR_LEFT, -SYSTEM_AVATAR_BOTTOM);
    lv_obj_add_event_cb(avatar_obj, system_footer_avatar_deleted, LV_EVENT_DELETE, NULL);
    avatar_set_visible(g_footer_avatar, false);

    /* Sibling above the footer so it can extend over the page; see system_ui_layout_footer. */
    g_reply_viewport = lv_obj_create(parent);
    floatair_assert(g_reply_viewport != NULL, "reply viewport create failed");
    lv_obj_remove_style_all(g_reply_viewport);
    lv_obj_remove_flag(g_reply_viewport, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                       LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                       LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_scroll_dir(g_reply_viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_reply_viewport, LV_SCROLLBAR_MODE_OFF);
    /* Opaque: the bubble covers page content beneath it. */
    lv_obj_set_style_bg_color(g_reply_viewport, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_reply_viewport, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(g_reply_viewport, SYSTEM_REPLY_PADDING_HOR, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(g_reply_viewport, SYSTEM_REPLY_PADDING_VER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_reply_viewport, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(g_reply_viewport, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_reply_viewport, SYSTEM_REPLY_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_radius(g_reply_viewport, SYSTEM_REPLY_RADIUS, LV_PART_MAIN);
    lv_obj_set_size(g_reply_viewport, 1, 1);
    lv_obj_align(g_reply_viewport, LV_ALIGN_BOTTOM_LEFT, SYSTEM_REPLY_LEFT, -SYSTEM_REPLY_EDGE_PADDING);
    lv_obj_add_flag(g_reply_viewport, LV_OBJ_FLAG_HIDDEN);
    lv_obj_null_on_delete(&g_reply_viewport);

    /* A plain label: the widget wrapper's font binding and layout passes are
     * not needed, since system_ui_layout_footer sizes it explicitly. */
    g_reply_label = lv_label_create(g_reply_viewport);
    floatair_assert(g_reply_label != NULL, "reply label create failed");
    lv_obj_remove_flag(g_reply_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_label_set_long_mode(g_reply_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_reply_label, "");
    lv_obj_set_style_text_font(g_reply_label, system_ui_reply_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(g_reply_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_reply_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(g_reply_label, SYSTEM_REPLY_LINE_SPACE, LV_PART_MAIN);
    lv_obj_set_size(g_reply_label, 1, 1);
    lv_obj_set_pos(g_reply_label, 0, 0);
    lv_obj_null_on_delete(&g_reply_label);
}

/**
 * @brief 创建系统级蓝牙断连全屏遮罩。
 * @param[in] parent 遮罩挂载的父节点。
 * @return 无返回值。
 */
static void system_bt_disconnect_overlay_create(lv_obj_t* parent) {
    lv_obj_t* status_bar_host = NULL;

    if (parent == NULL || g_bt_disconnect_overlay != NULL) {
        return;
    }

    floatair_assert(system_bt_disconnect_overlay_init_ui(parent, &g_bt_disconnect_overlay_ui),
                    "bt disconnect overlay init failed");
    g_bt_disconnect_overlay = container_get_obj(g_bt_disconnect_overlay_ui.overlay);
    floatair_assert(g_bt_disconnect_overlay != NULL, "bt disconnect overlay NULL");
    lv_obj_remove_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);

    status_bar_host = container_get_obj(g_bt_disconnect_overlay_ui.status_bar_host);
    floatair_assert(status_bar_host != NULL, "bt disconnect status bar host NULL");
    g_bt_disconnect_status_bar = status_bar_create_with_pos(
        status_bar_host,
        (int32_t)config_lcd.ui_width,
        NULL,
        STATUS_BAR_POS_TOP);
    floatair_assert(g_bt_disconnect_status_bar != NULL, "bt disconnect status bar create failed");
    system_ui_refresh_status_bar(g_bt_disconnect_status_bar);

    system_ui_refresh_bt_disconnect_overlay_text();
    floatair_info("bt disconnect overlay created: parent=%p overlay=%p size=%ux%u",
                  parent,
                  g_bt_disconnect_overlay,
                  (unsigned)config_lcd.ui_width,
                  (unsigned)config_lcd.ui_height);
}

/**
 * @brief 立即刷新指定状态栏的电量、充电态与缓存时间显示。
 * @param[in] status_bar 目标状态栏对象。
 * @return 无返回值。
 */
void system_ui_refresh_status_bar(lv_obj_t* status_bar) {
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("status bar refresh deferred: lcd off");
        return;
    }

    if (status_bar == NULL || !lv_obj_is_valid(status_bar)) {
        return;
    }

    status_bar_update_battery(status_bar, g_status_bar_battery);
    status_bar_update_charge_state(status_bar, g_status_bar_charge_state);
    status_bar_set_wear_detection_visible(status_bar, g_status_bar_wear_detection_visible);
    system_ui_set_status_bar_time_visible(status_bar, g_status_bar_time_reliable);
    if (g_status_bar_time_reliable && g_status_bar_time_valid) {
        (void)system_ui_refresh_status_bar_time(status_bar, g_status_bar_time_epoch);
    }
}

/**
 * @brief 在显示距离档位变化后同步刷新前后台页面场景布局。
 * @return 无返回值。
 */
void system_ui_refresh_display_distance_level(void) {
    if (!system_ui_apply_display_distance_level_if_needed()) {
        return;
    }

    lv_coord_t content_h = system_ui_get_page_content_height();
    floatair_info("refresh display distance level: app=%s content_h=%d",
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A",
                  (int)content_h);
    app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width, content_h);
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}

/**
 * @brief 立即将当前活动屏幕标记为脏并触发一次 LVGL 刷屏。
 * @return `true` 表示已触发刷屏，`false` 表示当前没有有效活动屏幕。
 */
bool system_ui_refresh_screen_now(void) {
    lv_obj_t* screen = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        g_screen_refresh_pending = true;
        floatair_dbg("refresh screen deferred: lcd off");
        return true;
    }

    screen = lv_screen_active();
    if (screen == NULL || !lv_obj_is_valid(screen)) {
        floatair_warn("refresh screen ignored: active screen invalid");
        return false;
    }

    floatair_info("refresh screen now: screen=%p", screen);
    lv_obj_invalidate(screen);
    lv_refr_now(NULL);
    return true;
}

void system_ui_request_frame(void) {
    lv_timer_t* refr_timer = NULL;

    if (floatair_lcd_get_state() == LCD_OFF) {
        return;
    }

    /* Only reschedules the refresh timer. If nothing was invalidated the
     * refresh is a no-op, so this is safe to call on every input event. */
    refr_timer = lv_display_get_refr_timer(NULL);
    if (refr_timer == NULL) {
        return;
    }
    lv_timer_ready(refr_timer);
}

/**
 * @brief 将电量值同步到顶部状态栏。
 * @param[in] battery 电量百分比。
 * @return 无返回值。
 */
void system_ui_update_battery(uint8_t battery) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_battery = battery;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("battery ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_battery(status_bar, battery);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_battery(g_bt_disconnect_status_bar, battery);
    }
}

/**
 * @brief 将充电状态同步到顶部状态栏。
 * @param[in] charge_state 充电状态值。
 * @return 无返回值。
 */
void system_ui_update_charge_state(uint8_t charge_state) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_charge_state = charge_state;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("charge state ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_update_charge_state(status_bar, charge_state);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_update_charge_state(g_bt_disconnect_status_bar, charge_state);
    }
}

/**
 * @brief 设置顶部状态栏佩戴检测图标显隐。
 * @param[in] visible `true` 表示显示图标占位，`false` 表示隐藏图标占位。
 * @return 无返回值。
 */
void system_ui_set_wear_detection_visible(bool visible) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_wear_detection_visible = visible;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("wear detection ui update deferred: lcd off");
        return;
    }

    status_bar = system_ui_get_current_status_bar();
    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        status_bar_set_wear_detection_visible(status_bar, visible);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        status_bar_set_wear_detection_visible(g_bt_disconnect_status_bar, visible);
    }
}

/**
 * @brief 按指定时间戳刷新顶部状态栏时间。
 * @param[in] time_now 需要显示的时间戳。
 * @return `true` 表示刷新成功，`false` 表示刷新失败。
 */
bool system_ui_update_time_from_epoch(time_t time_now) {
    lv_obj_t* status_bar = NULL;

    g_status_bar_time_epoch = time_now;
    g_status_bar_time_valid = true;
    if (floatair_lcd_get_state() == LCD_OFF) {
        g_status_bar_refresh_pending = true;
        floatair_dbg("time ui update deferred: lcd off");
        return true;
    }

    bool ok = true;
    status_bar = system_ui_get_current_status_bar();
    system_ui_set_status_bar_time_visible(status_bar, g_status_bar_time_reliable);
    system_ui_set_status_bar_time_visible(g_bt_disconnect_status_bar, g_status_bar_time_reliable);
    if (!g_status_bar_time_reliable) {
        return true;
    }

    if (status_bar != NULL && lv_obj_is_valid(status_bar)) {
        ok = system_ui_refresh_status_bar_time(status_bar, time_now);
    }
    if (g_bt_disconnect_status_bar != NULL && lv_obj_is_valid(g_bt_disconnect_status_bar)) {
        ok = system_ui_refresh_status_bar_time(g_bt_disconnect_status_bar, time_now) && ok;
    }
    return ok;
}

/**
 * @brief 亮屏后统一补刷灭屏期间延迟的系统 UI 更新。
 * @return 无返回值。
 */
void system_ui_flush_pending_after_screen_on(void) {
    bool refresh_screen = g_screen_refresh_pending;

    if (floatair_lcd_get_state() == LCD_OFF) {
        return;
    }

    if (g_status_bar_refresh_pending) {
        g_status_bar_refresh_pending = false;
        system_ui_refresh_status_bar(g_status_bar_top);
        system_ui_refresh_status_bar(g_bt_disconnect_status_bar);
    }

    if (refresh_screen) {
        g_screen_refresh_pending = false;
        (void)system_ui_refresh_screen_now();
    }
}

/**
 * @brief 在蓝牙断连遮罩显示时拦截页面输入事件。
 * @return `true` 表示事件已被遮罩吞掉，`false` 表示应继续分发。
 */
bool system_ui_try_intercept_bt_disconnect_overlay_input(void) {
    if (!system_bt_disconnect_overlay_is_active()) {
        return false;
    }

    floatair_info("bt disconnect overlay intercepted input");
    return true;
}

/**
 * @brief 按当前系统运行时状态统一同步系统壳层显隐与层级。
 * @return 无返回值。
 */
void system_ui_sync_shell_state(void) {
    bool bt_connected = system_get_btconn_state();
    bool overlay_visible = system_bt_disconnect_overlay_should_show();

    floatair_info("sync shell state: bt_connected=%d, overlay_target=%d, app=%s, current_app=%s, overlay_prev=%d",
                  (int)bt_connected,
                  (int)overlay_visible,
                  app_router_get_app(),
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A",
                  (int)g_bt_disconnect_overlay_visible);
    system_ui_set_bt_disconnect_overlay_visible(overlay_visible);
}

/**
 * @brief 控制蓝牙断连遮罩显隐，并同步状态栏层级。
 * @param[in] visible `true` 表示显示遮罩，`false` 表示隐藏遮罩。
 * @return 无返回值。
 */
void system_ui_set_bt_disconnect_overlay_visible(bool visible) {
    bool overlay_valid = g_bt_disconnect_overlay != NULL && lv_obj_is_valid(g_bt_disconnect_overlay);
    bool hidden_before = false;

    if (visible) (void)system_ui_set_reply("");

    if (overlay_valid) {
        hidden_before = lv_obj_has_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    floatair_info("set bt disconnect overlay visible: req=%d, prev=%d, valid=%d, hidden_before=%d, app=%s, current_app=%s",
                  (int)visible,
                  (int)g_bt_disconnect_overlay_visible,
                  (int)overlay_valid,
                  (int)hidden_before,
                  app_router_get_app(),
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A");

    if (g_bt_disconnect_overlay == NULL || !lv_obj_is_valid(g_bt_disconnect_overlay)) {
        floatair_warn("set bt disconnect overlay visible ignored: overlay invalid, req=%d", (int)visible);
        return;
    }

    system_ui_refresh_bt_disconnect_overlay_text();

    if (hidden_before == !visible) {
        floatair_info("set bt disconnect overlay visible skipped: unchanged req=%d", (int)visible);
        g_bt_disconnect_overlay_visible = visible;
        return;
    }

    g_bt_disconnect_overlay_visible = visible;
    if (visible) {
        uint32_t level = system_config_get_displaylevel();
        int32_t distance = system_get_displaylevel_value(level);

        system_notification_clear();
        toast_dismiss_active();
        (void)notify_list_close();
        jyt_dual_screen_set_root_distance((int)distance);
        g_display_level_applied = level;
        system_ui_sync_app_layer_scene();
        lv_obj_remove_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_bt_disconnect_overlay);
        system_ui_sync_app_layer_scene();
    } else {
        lv_obj_add_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN);
        system_ui_sync_app_layer_scene();
    }

    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }

    floatair_info("set bt disconnect overlay visible done: req=%d, hidden_after=%d, active=%d, status_bar_expected=%d",
                  (int)visible,
                  (int)lv_obj_has_flag(g_bt_disconnect_overlay, LV_OBJ_FLAG_HIDDEN),
                  (int)system_bt_disconnect_overlay_is_active(),
                  (int)g_status_bar_top_visible);
}

/**
 * @brief Initialize the system screen, page container, header, and footer.
 * @return 返回当前活动屏幕根对象，失败时触发断言。
 */
lv_obj_t* system_init_lvgl_fb(void) {
    lv_obj_t* p_root = lv_screen_active();
    lv_obj_t* page_parent = NULL;
    int32_t output_width = 0;
    int32_t output_height = 0;
    floatair_assert(p_root != NULL, "lv_screen_active failed");

    /* Pace animation and regular redraws at about 15 FPS in every state.
     * Input and reply updates can still request an immediate refresh. */
    lv_timer_t* animation_timer = lv_anim_get_timer();
    lv_timer_t* refresh_timer = lv_display_get_refr_timer(NULL);
    if (animation_timer != NULL) {
        lv_timer_set_period(animation_timer, SYSTEM_FRAME_PERIOD_MS);
    }
    if (refresh_timer != NULL) {
        lv_timer_set_period(refresh_timer, SYSTEM_FRAME_PERIOD_MS);
    }

    config_lcd.ui_x_begin = SYSTEM_LCD_UI_X_BEGIN;
    config_lcd.ui_y_begin = SYSTEM_LCD_UI_Y_BEGIN;
    config_lcd.ui_width = SYSTEM_LCD_UI_WIDTH;
    config_lcd.ui_height = SYSTEM_LCD_UI_HEIGHT;
    output_width = app_stereo_get_output_width();
    output_height = app_stereo_get_output_height();
    floatair_info("init lvgl fb: root=%p logical=%ux%u output=%dx%d",
                  p_root,
                  (unsigned)config_lcd.ui_width,
                  (unsigned)config_lcd.ui_height,
                  (int)output_width,
                  (int)output_height);
    lv_obj_remove_style_all(p_root);
    lv_obj_set_style_border_width(p_root, 0, LV_PART_MAIN);
    lv_obj_set_size(p_root, output_width, output_height);
    lv_obj_set_style_bg_color(p_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(p_root, true, LV_PART_MAIN);

    if (app_layers_init(p_root, (int32_t)config_lcd.ui_width, (int32_t)config_lcd.ui_height)) {
        page_parent = app_layers_get_app();
    } else {
        floatair_warn("app framework layers init failed");
        page_parent = p_root;
    }

    g_page_content = lv_obj_create(page_parent);
    floatair_assert(g_page_content != NULL, "page content create failed");
    lv_obj_remove_style_all(g_page_content);
    lv_obj_remove_flag(g_page_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_null_on_delete(&g_page_content);
    system_ui_layout_page_content();

    system_status_bar_top_create(page_parent);
    system_footer_create(page_parent);
    system_bt_disconnect_overlay_create(
        (app_layers_get_overlay() != NULL) ? app_layers_get_overlay() : p_root);
    floatair_info("init lvgl fb: defer shell sync until first page load");
    (void)system_ui_apply_display_distance_level_if_needed();
    system_ui_sync_app_layer_scene();
    system_ui_sync_avatar_state("init");
    return p_root;
}

/**
 * @brief 获取指定位置的系统状态栏对象。
 * @param[in] pos 状态栏位置。
 * @return 返回对应位置的状态栏对象；当前仅支持顶部状态栏。
 */
lv_obj_t* system_get_status_bar(status_bar_widget_pos_t pos) {
    if (pos != STATUS_BAR_POS_TOP) {
        return NULL;
    }

    return system_ui_get_current_status_bar();
}

/**
 * @brief 设置顶部状态栏显示模式，并同步页面布局与遮罩层级。
 * @param[in] show_top `true` 表示显示顶部状态栏，`false` 表示隐藏。
 * @return 无返回值。
 */
void system_status_bar_set_mode(bool show_top) {
    lv_coord_t content_h = 0;
    bool prev_show_top = true;

    prev_show_top = g_status_bar_top_visible;
    g_status_bar_top_visible = show_top;
    floatair_info("set status bar mode: prev=%d next=%d current_app=%s",
                  (int)prev_show_top,
                  (int)show_top,
                  app_manager_current_name() != NULL ? app_manager_current_name() : "N/A");

    if (prev_show_top == show_top) {
        return;
    }

    system_ui_layout_page_content();
    content_h = system_ui_get_page_content_height();
    app_manager_sync_current_view_layout((int32_t)config_lcd.ui_width, content_h);
    system_bt_disconnect_overlay_sync_status_bar();
    if (system_bt_disconnect_overlay_is_active()) {
        lv_obj_move_foreground(g_bt_disconnect_overlay);
    }
}
