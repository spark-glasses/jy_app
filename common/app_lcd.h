#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <lvgl.h>
#include "floatair_dbg.h"
#include "app_def.h"

/**
 * @brief LCD 亮灭屏状态；全系统统一使用 `0` 表示灭屏、`1` 表示亮屏。
 */
typedef enum
{
    LCD_OFF = 0,
    LCD_ON = 1
} lcd_state_t;

/**
 * @brief 判断 LCD 状态值是否符合 `0=灭屏、1=亮屏` 的统一约定。
 */
bool floatair_lcd_state_is_valid(lcd_state_t state);

/**
 * @brief 获取 LCD 状态的可读名称。
 * @return `"OFF"`、`"ON"` 或 `"INVALID"`。
 */
const char* floatair_lcd_state_name(lcd_state_t state);

/**
 * @brief 获取当前 LCD 亮灭屏状态。
 * @return `LCD_OFF(0)` 表示灭屏，`LCD_ON(1)` 表示亮屏。
 */
lcd_state_t floatair_lcd_get_state(void);

/**
 * @brief 判断 LCD 是否处于灭屏状态。
 * @return true 表示 LCD 处于灭屏状态，false 表示 LCD 处于亮屏状态。
 */
bool floatair_lcd_is_off(void);

/**
 * @brief 设置 LCD 亮灭屏状态。
 * @param state `LCD_OFF(0)` 表示灭屏，`LCD_ON(1)` 表示亮屏。
 */
void floatair_lcd_set_state(lcd_state_t state);

/**
 * @brief Set the LCD brightness
 * @param brightness Brightness value (0-255)
 */
void floatair_lcd_set_brightness(uint8_t brightness);

/**
 * @brief 立即使目标对象失效，并将其所属 display 的当前画面提交到 LCD。
 * @param[in] target 需要刷新的目标 LVGL 对象。
 * @return 无返回值。
 */
void floatair_lcd_commit_frame(lv_obj_t* target);

/**
 * @brief Get the current LCD brightness
 * @return uint16_t Current LCD brightness (0-255)
 */
uint16_t floatair_lcd_get_brightness(void);
