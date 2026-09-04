/**
 * @file classic_msgbox.c
 * @brief Classic 双按钮消息确认框实现。
 */
#include "msgbox.h"

#include <string.h>

#include "button.h"
#include "common/app_framework/app_layers.h"
#include "container.h"
#include "label.h"
#include "system/system_res.h"
#include "ui_widget.h"

typedef struct msgbox_t msgbox_t;

typedef enum {
    MSGBOX_KEY_NONE = 0,         ///< 当前没有确认选择。
    MSGBOX_KEY_LEFT = 1 << 0,    ///< 当前确认了左侧按钮。
    MSGBOX_KEY_RIGHT = 1 << 1,   ///< 当前确认了右侧按钮。
    MSGBOX_KEY_CANCEL = 1 << 2,  ///< 当前确认了取消按钮。
    MSGBOX_KEY_CONFIRM = 1 << 3, ///< 当前确认了确认按钮。
    MSGBOX_KEY_DISMISS = 1 << 4, ///< 消息框被关闭，但未确认任一按钮。
} msgbox_key_t;

typedef void (*msgbox_event_cb_t)(msgbox_t* box, msgbox_key_t key, void* user_data);

typedef struct {
    msgbox_key_t key; ///< 按钮语义；未定义时回退为左右侧语义。
    const char* text; ///< 按钮文案；为空时按语义使用默认文案。
} msgbox_action_t;

typedef struct {
    container_cfg_t dlg;      ///< 弹窗主容器配置。
    label_cfg_t title;        ///< 标题文本默认配置。
    container_cfg_t actions;  ///< 按钮行容器配置。
    button_cfg_t button;      ///< 按钮的默认样式配置。
    int32_t title_width;      ///< 标题文本布局宽度。
    int32_t top_pad;          ///< 标题距离弹窗顶部的间距。
    int32_t bottom_pad;       ///< 按钮区域距离弹窗底部的间距。
    int32_t title_button_pad; ///< 标题与按钮的间距。
    int32_t button_x_pad;     ///< 按钮距左右边缘的横向边距。
    const char* message;      ///< 顶部提示文本。
    msgbox_action_t left;     ///< 左侧按钮配置。
    msgbox_action_t right;    ///< 右侧按钮配置。
} msgbox_cfg_t;

/**
 * @brief 消息框实例，保存弹窗本体、标题、左右按钮和当前选中状态。
 */
struct msgbox_t {
    container_t* dlg;     ///< 最外层弹窗容器。
    label_t* title;       ///< 顶部提示文本组件。
    container_t* actions;///< 底部按钮行布局容器。
    button_t* btn_left;   ///< 左侧按钮组件。
    button_t* btn_right;  ///< 右侧按钮组件。
    msgbox_key_t sel;     ///< 当前高亮的按钮。
    msgbox_cfg_t cfg;     ///< 当前生效的配置缓存。
    msgbox_event_cb_t cb; ///< 确认结果回调。
    void* user_data;      ///< 回调透传数据。
};

static msgbox_t* s_active_msgbox = NULL; ///< 当前活动消息框。
static msgbox_t* s_local_msgbox = NULL; ///< 统一接口创建的本地确认框。
static msgbox_choice_cb_t s_local_choice_cb = NULL; ///< 本地确认结果回调。
static void* s_local_choice_user_data = NULL; ///< 本地确认回调透传数据。

/**
 * @brief 获取弹层应挂载的父对象。
 *
 * 优先挂到全局弹窗层，使消息框不再依赖当前页面栈；
 * 弹窗层不可用时回退到当前活动屏幕。
 *
 * @return 返回弹层父对象；都不可用时返回 `NULL`。
 */
static lv_obj_t* msgbox_get_parent(void)
{
    lv_obj_t* popup = app_layers_get_popup();
    if (popup != NULL && lv_obj_is_valid(popup)) {
        return popup;
    }

    return lv_screen_active();
}

/**
 * @brief 判断消息框句柄及内部对象是否有效。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示有效，`false` 表示无效。
 */
static bool msgbox_is_valid(const msgbox_t* box)
{
    return box && box->dlg && container_is_valid(box->dlg);
}

/**
 * @brief 获取默认消息框配置。
 *
 * @return 返回填充好默认值的消息框配置结构体。
 */
static msgbox_cfg_t msgbox_default_cfg(void)
{
    msgbox_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.dlg = container_default_cfg();
    cfg.dlg.w = 330;
    cfg.dlg.h = LV_SIZE_CONTENT;
    cfg.dlg.opa = LV_OPA_COVER;
    cfg.dlg.radius = 18;
    cfg.dlg.border_width = 2;
    cfg.title = label_default_cfg();
    cfg.actions = container_default_cfg();
    cfg.button = button_default_cfg();
    cfg.title_width = 0;
    cfg.top_pad = 24;
    cfg.bottom_pad = 20;
    cfg.title_button_pad = 18;
    cfg.button_x_pad = 24;
    cfg.message = "";
    cfg.left.key = MSGBOX_KEY_CANCEL;
    cfg.right.key = MSGBOX_KEY_CONFIRM;
    /* 按钮文案在应用配置时按实际语义延迟解析，避免未使用的默认 i18n 查询。 */
    cfg.left.text = NULL;
    cfg.right.text = NULL;
    return cfg;
}

/**
 * @brief 获取预定义语义键对应的默认按钮文案。
 *
 * @param key 按钮语义键。
 * @return 返回对应默认文案；未定义语义返回 `NULL`。
 */
static const char* msgbox_key_default_text(msgbox_key_t key)
{
    switch (key) {
    case MSGBOX_KEY_CANCEL:
        return app_get_str("MSGBOX_CANCEL");
    case MSGBOX_KEY_CONFIRM:
        return app_get_str("MSGBOX_CONFIRM");
    default:
        return NULL;
    }
}

/**
 * @brief 判断按键是否属于预定义语义键。
 *
 * @param key 待判断的按键值。
 * @return `true` 表示属于已支持的语义键，`false` 表示不是。
 */
static bool msgbox_is_semantic_key(msgbox_key_t key)
{
    switch (key) {
    case MSGBOX_KEY_CANCEL:
    case MSGBOX_KEY_CONFIRM:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 判断按钮配置是否需要显示。
 *
 * @param action 按钮配置。
 * @return `true` 表示按钮应显示，`false` 表示隐藏。
 */
static bool msgbox_action_visible(const msgbox_action_t* action)
{
    return action && (action->key != MSGBOX_KEY_NONE || (action->text && action->text[0]));
}

/**
 * @brief 根据当前选中侧解析消息框关闭时的最终返回值。
 *
 * 若当前侧配置了预定义语义键，则返回语义键；
 * 否则回退为 `MSGBOX_KEY_LEFT` 或 `MSGBOX_KEY_RIGHT`。
 *
 * @param box 目标消息框组件句柄。
 * @param side 当前选中的按钮侧别。
 * @return 返回最终确认结果。
 */
static msgbox_key_t msgbox_resolve_result(const msgbox_t* box, msgbox_key_t side)
{
    msgbox_key_t key = MSGBOX_KEY_NONE;

    if (!box) {
        return MSGBOX_KEY_NONE;
    }

    if (side == MSGBOX_KEY_LEFT) {
        key = box->cfg.left.key;
        return msgbox_is_semantic_key(key) ? key : MSGBOX_KEY_LEFT;
    }

    if (side == MSGBOX_KEY_RIGHT) {
        key = box->cfg.right.key;
        return msgbox_is_semantic_key(key) ? key : MSGBOX_KEY_RIGHT;
    }

    return MSGBOX_KEY_NONE;
}

/**
 * @brief 根据当前选中项刷新左右按钮的高亮状态。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
static void msgbox_apply_btn_state(msgbox_t* box)
{
    lv_opa_t sel_opa = LV_OPA_COVER;
    lv_opa_t unsel_opa = (lv_opa_t)(LV_OPA_COVER * 70 / 100);

    if (!box) {
        return;
    }

    if (box->btn_left) {
        button_set_opacity(box->btn_left, box->sel == MSGBOX_KEY_LEFT ? sel_opa : unsel_opa);
    }

    if (box->btn_right) {
        button_set_opacity(box->btn_right, box->sel == MSGBOX_KEY_RIGHT ? sel_opa : unsel_opa);
    }
}

/**
 * @brief 刷新弹窗的布局容器和尺寸。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
static void msgbox_sync_layout(msgbox_t* box)
{
    const msgbox_cfg_t* cfg = NULL;
    lv_obj_t* dlg_obj = NULL;
    lv_obj_t* title_obj = NULL;
    lv_obj_t* left_obj = NULL;
    lv_obj_t* right_obj = NULL;
    bool left_visible = false;
    bool right_visible = false;
    lv_coord_t actions_w = 0;
    lv_coord_t title_w = 0;

    if (!msgbox_is_valid(box)) {
        return;
    }
    cfg = &box->cfg;
    dlg_obj = container_get_obj(box->dlg);
    title_obj = box->title ? label_get_obj(box->title) : NULL;
    left_obj = box->btn_left ? button_get_obj(box->btn_left) : NULL;
    right_obj = box->btn_right ? button_get_obj(box->btn_right) : NULL;
    left_visible = msgbox_action_visible(&cfg->left);
    right_visible = msgbox_action_visible(&cfg->right);
    if (!dlg_obj || !box->actions || !title_obj || !left_obj || !right_obj) {
        return;
    }

    ui_widget_set_size(UI_WIDGET(box->dlg), (lv_coord_t)cfg->dlg.w, LV_SIZE_CONTENT);
    container_set_opacity(box->dlg, cfg->dlg.opa);
    container_set_border_width(box->dlg, (lv_coord_t)cfg->dlg.border_width);
    container_set_radius(box->dlg, (lv_coord_t)cfg->dlg.radius);
    lv_obj_set_style_pad_top(dlg_obj, (lv_coord_t)cfg->top_pad, 0);
    lv_obj_set_style_pad_bottom(dlg_obj, (lv_coord_t)cfg->bottom_pad, 0);
    lv_obj_set_style_pad_left(dlg_obj, (lv_coord_t)cfg->dlg.pad_hor, 0);
    lv_obj_set_style_pad_right(dlg_obj, (lv_coord_t)cfg->dlg.pad_hor, 0);
    lv_obj_align(dlg_obj, LV_ALIGN_CENTER, 0, 0);

    title_w = (lv_coord_t)cfg->title_width;
    if (title_w <= 0 || title_w == LV_SIZE_CONTENT) {
        title_w = (lv_coord_t)cfg->dlg.w - 2 * (lv_coord_t)cfg->button_x_pad;
        if (title_w < 0) {
            title_w = 0;
        }
    }
    ui_widget_set_size(UI_WIDGET(box->title), title_w, LV_SIZE_CONTENT);

    actions_w = (lv_coord_t)cfg->button.w;
    if (left_visible && right_visible) {
        actions_w = (lv_coord_t)cfg->dlg.w - 2 * (lv_coord_t)cfg->button_x_pad;
        if (actions_w < 2 * (lv_coord_t)cfg->button.w) {
            actions_w = 2 * (lv_coord_t)cfg->button.w;
        }
    }
    ui_widget_set_size(UI_WIDGET(box->actions), actions_w, LV_SIZE_CONTENT);

    ui_widget_set_size(UI_WIDGET(box->btn_left), (lv_coord_t)cfg->button.w, (lv_coord_t)cfg->button.h);
    ui_widget_set_size(UI_WIDGET(box->btn_right), (lv_coord_t)cfg->button.w, (lv_coord_t)cfg->button.h);
}

/**
 * @brief 弹窗删除回调，负责释放消息框对象自身。
 *
 * @param e LVGL 删除事件。
 * @return 无返回值。
 */
static void msgbox_on_delete(lv_event_t* e)
{
    msgbox_t* box = (msgbox_t*)lv_event_get_user_data(e);

    if (!box) {
        return;
    }

    if (s_active_msgbox == box) {
        s_active_msgbox = NULL;
    }
    if (s_local_msgbox == box) {
        s_local_msgbox = NULL;
        s_local_choice_cb = NULL;
        s_local_choice_user_data = NULL;
    }

    box->dlg = NULL;
    box->title = NULL;
    box->actions = NULL;
    box->btn_left = NULL;
    box->btn_right = NULL;
    lv_free(box);
}

/**
 * @brief 按按钮配置刷新指定按钮的显示文案。
 *
 * @param box 目标消息框组件句柄。
 * @param button 目标按钮组件句柄。
 * @param action 按钮配置；传 `NULL` 时回退为默认文案。
 * @param fallback_text 当语义和文案都不可用时使用的兜底文案。
 * @return 无返回值。
 */
static void msgbox_apply_action(msgbox_t* box,
                                button_t* button,
                                const msgbox_action_t* action,
                                const char* fallback_text)
{
    const char* resolved = NULL;
    msgbox_key_t key = MSGBOX_KEY_NONE;

    if (!msgbox_is_valid(box) || !button) {
        return;
    }

    if (action) {
        key = action->key;
        resolved = (action->text && action->text[0]) ? action->text : msgbox_key_default_text(key);
    }

    button_set_text(button, resolved ? resolved : fallback_text);
}

/**
 * @brief 设置消息框提示文本。
 * @param[in] box 目标消息框对象。
 * @param[in] message 新的提示文本。
 * @return 无返回值。
 */
static void msgbox_set_text(msgbox_t* box, const char* message)
{
    if (!msgbox_is_valid(box) || !box->title || !lv_obj_is_valid(label_get_obj(box->title))) {
        return;
    }

    label_set_text(box->title, (message && message[0]) ? message : " ");
    msgbox_sync_layout(box);
}

/**
 * @brief 批量应用消息框配置。
 *
 * @param box 目标消息框组件句柄。
 * @param cfg 消息框配置；传 `NULL` 时使用默认配置。
 * @return 无返回值。
 */
static void msgbox_apply_cfg(msgbox_t* box, const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;

    if (!msgbox_is_valid(box)) {
        return;
    }

    if (!cfg) {
        default_cfg = msgbox_default_cfg();
        cfg = &default_cfg;
    }
    box->cfg = *cfg;

    container_set_layout_vbox_spaced(box->dlg, cfg->title_button_pad);
    container_set_align(box->dlg,
                        CONTAINER_ALIGN_START,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    container_set_layout_hbox(box->actions);
    container_set_align(box->actions,
                        msgbox_action_visible(&cfg->left) && msgbox_action_visible(&cfg->right)
                            ? CONTAINER_ALIGN_SPACE_BETWEEN
                            : CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER,
                        CONTAINER_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container_get_obj(box->actions), 0, 0);

    label_apply_cfg(box->title, &cfg->title);
    button_apply_cfg(box->btn_left, &cfg->button);
    button_apply_cfg(box->btn_right, &cfg->button);
    label_set_align(box->title, LABEL_ALIGN_CENTER);
    label_set_overflow(box->title, LABEL_OVERFLOW_WRAP);
    msgbox_apply_action(box, box->btn_left, &cfg->left, "Left");
    msgbox_apply_action(box, box->btn_right, &cfg->right, "Right");
    ui_widget_set_visible(UI_WIDGET(box->btn_left), msgbox_action_visible(&cfg->left));
    ui_widget_set_visible(UI_WIDGET(box->btn_right), msgbox_action_visible(&cfg->right));
    msgbox_set_text(box, cfg->message);
}

/**
 * @brief 创建消息框组件。
 *
 * @param cfg 消息框配置；传 `NULL` 时使用默认配置。
 * @return 创建成功返回消息框组件句柄，失败返回 `NULL`。
 */
static msgbox_t* msgbox_create(const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;
    msgbox_t* box = NULL;
    lv_obj_t* parent = NULL;
    if (!cfg) {
        default_cfg = msgbox_default_cfg();
        cfg = &default_cfg;
    }

    box = (msgbox_t*)lv_malloc(sizeof(msgbox_t));
    if (!box) {
        return NULL;
    }
    memset(box, 0, sizeof(*box));
    box->sel = MSGBOX_KEY_RIGHT;
    box->cfg = *cfg;

    parent = msgbox_get_parent();
    if (!parent) {
        lv_free(box);
        return NULL;
    }

    box->dlg = container_create(parent, NULL);
    if (!box->dlg) {
        lv_free(box);
        return NULL;
    }
    lv_obj_add_event_cb(container_get_obj(box->dlg), msgbox_on_delete, LV_EVENT_DELETE, box);

    box->title = label_create(container_get_obj(box->dlg), NULL);

    box->actions = container_create(container_get_obj(box->dlg), NULL);

    box->btn_left = button_create(container_get_obj(box->actions), NULL);
    box->btn_right = button_create(container_get_obj(box->actions), NULL);

    if (!box->title || !box->actions || !box->btn_left || !box->btn_right) {
        ui_widget_destroy(UI_WIDGET(box->dlg));
        return NULL;
    }

    msgbox_apply_cfg(box, cfg);
    ui_widget_set_visible(UI_WIDGET(box->dlg), false);
    return box;
}

/**
 * @brief 销毁消息框组件并释放对象。
 *
 * @param box 目标消息框组件句柄。
 * @return 无返回值。
 */
static void msgbox_destroy(msgbox_t* box)
{
    if (!box) {
        return;
    }

    if (msgbox_is_valid(box)) {
        ui_widget_destroy(UI_WIDGET(box->dlg));
        return;
    }

    lv_free(box);
}

/**
 * @brief 设置消息框显隐状态。
 *
 * @param box 目标消息框组件句柄。
 * @param show `true` 表示显示，`false` 表示隐藏。
 * @return 无返回值。
 */
static void msgbox_set_visible(msgbox_t* box, bool show)
{
    if (!msgbox_is_valid(box)) {
        return;
    }

    if (show) {
        box->sel = msgbox_action_visible(&box->cfg.right) ? MSGBOX_KEY_RIGHT : MSGBOX_KEY_LEFT;
        msgbox_sync_layout(box);
        msgbox_apply_btn_state(box);
        ui_widget_move_foreground(UI_WIDGET(box->dlg));
        ui_widget_set_visible(UI_WIDGET(box->dlg), true);
        s_active_msgbox = box;
    } else {
        ui_widget_set_visible(UI_WIDGET(box->dlg), false);
        if (s_active_msgbox == box) {
            s_active_msgbox = NULL;
        }
    }
}

/**
 * @brief 创建或复用一个消息框，并立即显示。
 *
 * @param box 调用者当前持有的消息框对象；传 `NULL` 时会新建。
 * @param cfg 消息框配置。
 * @return 返回可继续持有的消息框对象，失败返回 `NULL`。
 */
static msgbox_t* msgbox_show_with_cfg(msgbox_t* box, const msgbox_cfg_t* cfg)
{
    msgbox_cfg_t default_cfg;
    lv_obj_t* parent = msgbox_get_parent();

    if (!cfg) {
        default_cfg = msgbox_default_cfg();
        cfg = &default_cfg;
    }

    if (msgbox_is_valid(box) && parent &&
        ui_widget_get_parent(UI_WIDGET(box->dlg)) != parent) {
        msgbox_destroy(box);
        box = NULL;
    }

    if (!msgbox_is_valid(box)) {
        box = msgbox_create(cfg);
        if (!box) {
            return NULL;
        }
    } else {
        msgbox_apply_cfg(box, cfg);
    }

    msgbox_set_visible(box, true);
    return box;
}

/**
 * @brief 判断消息框当前是否隐藏。
 *
 * @param box 目标消息框组件句柄。
 * @return `true` 表示隐藏或对象无效，`false` 表示当前可见。
 */
static bool msgbox_is_hidden(const msgbox_t* box)
{
    if (!msgbox_is_valid(box)) {
        return true;
    }

    return ui_widget_is_hidden(UI_WIDGET(box->dlg));
}

/**
 * @brief 处理消息框按键或手势输入。
 *
 * @param box 目标消息框组件句柄。
 * @param code LVGL 事件码。
 * @return 返回最终确认的按钮；仅切换高亮时返回 `MSGBOX_KEY_NONE`。
 */
static msgbox_key_t msgbox_key_handler(msgbox_t* box, lv_event_code_t code)
{
    if (!msgbox_is_valid(box) || msgbox_is_hidden(box)) {
        return MSGBOX_KEY_NONE;
    }

    switch (code) {
    case LV_EVENT_GESTURE_RIGHT:
        box->sel = msgbox_action_visible(&box->cfg.right) ? MSGBOX_KEY_RIGHT : MSGBOX_KEY_LEFT;
        msgbox_apply_btn_state(box);
        break;
    case LV_EVENT_GESTURE_LEFT:
        box->sel = msgbox_action_visible(&box->cfg.left) ? MSGBOX_KEY_LEFT : MSGBOX_KEY_RIGHT;
        msgbox_apply_btn_state(box);
        break;
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        msgbox_set_visible(box, false);
        return msgbox_resolve_result(box, box->sel);
    default:
        break;
    }

    return MSGBOX_KEY_NONE;
}

/**
 * @brief 处理当前活动消息框输入事件。
 * @param[in] code LVGL 事件码。
 * @return 返回事件处理结果。
 */
msgbox_event_result_t msgbox_handle_active_event(lv_event_code_t code)
{
    msgbox_t* box = s_active_msgbox;
    msgbox_key_t result = MSGBOX_KEY_NONE;

    if (!msgbox_is_valid(box) || msgbox_is_hidden(box)) {
        s_active_msgbox = NULL;
        return MSGBOX_EVENT_IGNORED;
    }

    switch (code) {
    case LV_EVENT_DCLICKED:
        msgbox_set_visible(box, false);
        if (box->cb) {
            box->cb(box, MSGBOX_KEY_DISMISS, box->user_data);
        }
        return MSGBOX_EVENT_CONSUMED_LOCAL;
    case LV_EVENT_GESTURE_RIGHT:
    case LV_EVENT_GESTURE_LEFT:
    case LV_EVENT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
        result = msgbox_key_handler(box, code);
        if (result != MSGBOX_KEY_NONE && box->cb) {
            box->cb(box, result, box->user_data);
        }
        return MSGBOX_EVENT_CONSUMED_LOCAL;
    default:
        return MSGBOX_EVENT_IGNORED;
    }
}

/**
 * @brief 将 Classic MsgBox 按键结果转换为统一确认结果。
 * @param[in] box 消息框对象。
 * @param[in] key Classic 按键结果。
 * @param[in] user_data 未使用。
 * @return 无返回值。
 */
static void classic_msgbox_on_local_result(msgbox_t* box,
                                            msgbox_key_t key,
                                            void* user_data) {
    msgbox_choice_cb_t choice_cb = s_local_choice_cb;
    void* choice_user_data = s_local_choice_user_data;

    (void)user_data;
    s_local_choice_cb = NULL;
    s_local_choice_user_data = NULL;
    if (s_local_msgbox == box) {
        msgbox_destroy(box);
    }
    if (choice_cb == NULL) {
        return;
    }
    choice_cb(key == MSGBOX_KEY_CONFIRM ? MSGBOX_CHOICE_CONFIRM
                                        : MSGBOX_CHOICE_CANCEL,
              choice_user_data);
}

/**
 * @brief 显示 Classic 本地确认框。
 * @param[in] title 确认内容。
 * @param[in] cancel_action 取消按钮文案。
 * @param[in] confirm_action 确认按钮文案。
 * @param[in] choice_cb 结果回调。
 * @param[in] user_data 调用方透传数据。
 * @return 成功返回 `true`，失败返回 `false`。
 */
bool msgbox_show_local(const char* title,
                       const char* cancel_action,
                       const char* confirm_action,
                       msgbox_choice_cb_t choice_cb,
                       void* user_data) {
    msgbox_cfg_t cfg = msgbox_default_cfg();

    if (title == NULL || cancel_action == NULL || confirm_action == NULL ||
        choice_cb == NULL) {
        return false;
    }

    cfg.message = title;
    cfg.left.key = MSGBOX_KEY_CANCEL;
    cfg.left.text = cancel_action;
    cfg.right.key = MSGBOX_KEY_CONFIRM;
    cfg.right.text = confirm_action;
    s_local_msgbox = msgbox_show_with_cfg(s_local_msgbox, &cfg);
    if (s_local_msgbox == NULL) {
        return false;
    }
    s_local_choice_cb = choice_cb;
    s_local_choice_user_data = user_data;
    s_local_msgbox->cb = classic_msgbox_on_local_result;
    s_local_msgbox->user_data = NULL;
    return true;
}

/**
 * @brief 关闭 Classic 本地确认框。
 * @return 无返回值。
 */
void msgbox_dismiss_local(void) {
    if (s_local_msgbox != NULL) {
        msgbox_destroy(s_local_msgbox);
        s_local_msgbox = NULL;
    }
    s_local_choice_cb = NULL;
    s_local_choice_user_data = NULL;
}

/**
 * @brief 判断 Classic 本地确认框是否正在显示。
 * @return 正在显示返回 `true`，否则返回 `false`。
 */
bool msgbox_is_local_active(void) {
    return msgbox_is_valid(s_local_msgbox) && !msgbox_is_hidden(s_local_msgbox);
}

/**
 * @brief 判断 Classic 实现是否支持手机 TapMsgbox 协议。
 * @return 始终返回 `false`。
 */
bool msgbox_remote_supported(void) {
    return false;
}

/**
 * @brief 拒绝显示手机协议下发的 TapMsgBox。
 * @param[in] title 第一行正文。
 * @param[in] hint 第二行操作提示。
 * @return 始终返回 `false`。
 */
bool msgbox_show_remote(const char* title, const char* hint) {
    (void)title;
    (void)hint;
    return false;
}

/**
 * @brief 拒绝显示手机协议下发的下载进度框。
 * @param[in] title 第一行正文。
 * @param[in] progress 进度值。
 * @return 始终返回 `false`。
 */
bool msgbox_show_remote_progress(const char* title, uint8_t progress) {
    (void)title;
    (void)progress;
    return false;
}

/**
 * @brief 处理 Classic 产品的远端关闭请求。
 * @return 无返回值。
 */
void msgbox_dismiss_remote(void) {
}
