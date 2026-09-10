#include "floatair_dbg.h"
#include "app_lcd.h"
#include "elf_common.h"
#include <nuttx/lcd/lcd_dev.h>
#include <lvgl.h>
#include "lvgl/src/core/lv_refr_private.h"
#include "common/message.h"
#include "system/system.h"
#include "system/system_runtime_ui.h"
#include "system/system_timer.h"

static lcd_state_t current_lcd_state = LCD_ON;
static uint8_t current_lcd_brightness = UINT8_MAX; ///< 最近一次设置到 LCD 硬件的亮度。

bool floatair_lcd_state_is_valid(lcd_state_t state)
{
    return state == LCD_OFF || state == LCD_ON;
}

const char* floatair_lcd_state_name(lcd_state_t state)
{
    if (state == LCD_OFF) {
        return "OFF";
    }
    if (state == LCD_ON) {
        return "ON";
    }
    return "INVALID";
}

/**
 * @brief 亮屏恢复后强制标记完整显示区域，确保双眼画面都重新刷新。
 * @return 无返回值。
 */
static void floatair_lcd_invalidate_full_display(void)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp == NULL) {
        return;
    }

    int32_t hor_res = lv_display_get_horizontal_resolution(disp);
    int32_t ver_res = lv_display_get_vertical_resolution(disp);
    if (hor_res <= 0 || ver_res <= 0) {
        return;
    }

    lv_area_t area;
    lv_area_set(&area, 0, 0, hor_res - 1, ver_res - 1);
    lv_inv_area(disp, &area);
    floatair_info("lcd wake invalidate full display %ldx%ld", (long)hor_res, (long)ver_res);
}

lcd_state_t floatair_lcd_get_state(void)
{
    floatair_info("current lcd state: %u(%s)",
                  (unsigned)current_lcd_state,
                  floatair_lcd_state_name(current_lcd_state));
    return current_lcd_state;
}

/**
 * @brief 判断 LCD 是否处于灭屏状态。
 * @return true 表示 LCD 已灭屏，false 表示 LCD 处于亮屏状态。
 */
bool floatair_lcd_is_off(void)
{
    return current_lcd_state == LCD_OFF;
}

void floatair_lcd_set_state(lcd_state_t state)
{
    if (!floatair_lcd_state_is_valid(state)) {
        floatair_err("reject invalid lcd state: %d, expected 0(OFF) or 1(ON)", (int)state);
        return;
    }
    floatair_info("lcd state: %u(%s) -> %u(%s)",
                  (unsigned)current_lcd_state,
                  floatair_lcd_state_name(current_lcd_state),
                  (unsigned)state,
                  floatair_lcd_state_name(state));
    if (current_lcd_state == state) {
        return;
    }
    if (state == LCD_ON) {
        current_lcd_state = state;
        system_timer_os_sleep_delay_cancel();
        floatair_lcd_set_brightness(system_runtime_state_get_lcd_resume_brightness());
        system_update_time();
        floatair_lcd_invalidate_full_display();
        system_runtime_state_flush_pending_after_screen_on();
        system_timer_flush_pending_after_screen_on();
        app_message_flush_pending_after_screen_on();
        system_ui_flush_pending_after_screen_on();
    } else {
        system_ui_render_screen_off_frame();
        current_lcd_state = state;
        floatair_lcd_set_brightness(0);
        if (!system_timer_os_sleep_delay_start()) {
            floatair_warn("start os sleep delay timer failed");
        }
    }
}

void floatair_lcd_set_brightness(uint8_t brightness)
{
    int ret=0;
    floatair_info("set lcd brightness: %d", brightness);
    int fd = *(int *)lv_display_get_driver_data(lv_display_get_default());
    ret = ioctl(fd, LCDDEVIO_SETPOWER, (long)brightness);
    if (ret < 0)
    {
        floatair_err("Error: ioctl(LCDDEVIO_SETPOWER) failed");
        return;
    }
    current_lcd_brightness = brightness;
    return;
}

void floatair_lcd_commit_frame(lv_obj_t* target)
{
    if (target == NULL || !lv_obj_is_valid(target)) {
        return;
    }

    lv_obj_invalidate(target);
    lv_refr_now(lv_obj_get_display(target));
}

uint16_t floatair_lcd_get_brightness(void)
{
    floatair_info("current_lcd_brightness: %u", (unsigned)current_lcd_brightness);
    return current_lcd_brightness;
}
