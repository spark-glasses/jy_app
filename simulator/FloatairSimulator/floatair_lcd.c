#include "../../common/app_lcd.h"
#include "../../common/message.h"
#include "elf_common.h"
#include "../../system/system.h"
#include "../../system/system_runtime_ui.h"
#include "../../system/system_timer.h"
#include "sys_adapter.h"
#include "floatair_dbg.h"

static lcd_state_t current_lcd_state = LCD_ON;
static uint8_t current_lcd_brightness = UINT8_MAX;

bool floatair_lcd_state_is_valid(lcd_state_t state) {
    return state == LCD_OFF || state == LCD_ON;
}

const char* floatair_lcd_state_name(lcd_state_t state) {
    if (state == LCD_OFF) {
        return "OFF";
    }
    if (state == LCD_ON) {
        return "ON";
    }
    return "INVALID";
}

lcd_state_t floatair_lcd_get_state(void) {
    return current_lcd_state;
}
/**
 * @brief 判断 LCD 是否处于灭屏状态。
 * @return true 表示 LCD 已灭屏，false 表示 LCD 处于亮屏状态。
 */
bool floatair_lcd_is_off(void) {
    return current_lcd_state == LCD_OFF;
}
void floatair_lcd_set_state(lcd_state_t state) {
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
        system_runtime_state_flush_pending_after_screen_on();
        system_timer_flush_pending_after_screen_on();
        app_message_flush_pending_after_screen_on();
        system_ui_flush_pending_after_screen_on();
    } else {
        system_ui_render_screen_off_frame();
        current_lcd_state = state;
        simulator_update_lcd_visual(current_lcd_brightness, state);
        if (!system_timer_os_sleep_delay_start()) {
            floatair_warn("start os sleep delay timer failed");
        }
    }
}
void floatair_lcd_set_brightness(uint8_t brightness) {
    current_lcd_brightness = brightness;
    floatair_info("set lcd brightness (Linux): %u", (unsigned)brightness);
    simulator_update_lcd_visual(current_lcd_brightness, current_lcd_state);
}
void floatair_lcd_commit_frame(lv_obj_t* target) {
    if (target == NULL || !lv_obj_is_valid(target)) {
        return;
    }

    lv_obj_invalidate(target);
    simulator_refresh_display_sync(lv_obj_get_display(target));
}
uint16_t floatair_lcd_get_brightness(void) {
    floatair_info("get lcd brightness (Linux): %u", (unsigned)current_lcd_brightness);
    return current_lcd_brightness;
}
