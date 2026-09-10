/**
 * @file home_units.c
 * @brief Home 应用单元查询和配置过滤实现。
 */
#include "home.h"

#include "common/app_framework/app_router.h"
#include "floatair_dbg.h"
#include "product_app.h"
#include "system/popups/assistant/assistant.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief 在产品静态单元数组中查找指定应用。
 * @param[in] app_name App 协议名称。
 * @return 找到时返回静态单元地址，否则返回 `NULL`。
 */
static const app_home_unit_t* home_units_find_supported(const char* app_name) {
    if (app_name == NULL || app_name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < g_home_units_count; ++i) {
        if (g_home_units_arr[i].name != NULL &&
            strcmp(g_home_units_arr[i].name, app_name) == 0) {
            return &g_home_units_arr[i];
        }
    }
    return NULL;
}

/**
 * @brief 判断动态单元数组是否已经包含指定应用。
 * @param[in] units 单元数组。
 * @param[in] count 单元数量。
 * @param[in] app_name App 协议名称。
 * @return 已包含时返回 `true`，否则返回 `false`。
 */
static bool home_units_contains(const app_home_unit_t* units,
                                size_t count,
                                const char* app_name) {
    if (units == NULL || app_name == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (units[i].name != NULL && strcmp(units[i].name, app_name) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 判断指定 App 是否属于当前产品 Home 支持范围。
 * @param[in] app_name App 协议名称。
 * @return 静态单元数组包含该 App 时返回 `true`，否则返回 `false`。
 */
bool home_is_supported_app(const char* app_name) {
    return home_units_find_supported(app_name) != NULL;
}

/**
 * @brief 获取指定 App 在 Home 中使用的国际化展示名称。
 * @param[in] app_name App 协议名称。
 * @return 找到有效展示名称时返回字符串地址，否则返回 `NULL`。
 */
const char* home_get_app_display_name(const char* app_name) {
    const app_home_unit_t* unit = home_units_find_supported(app_name);
    const char* display_name = NULL;

    if (unit == NULL || unit->icontext == NULL) {
        return NULL;
    }
    display_name = app_get_str(unit->icontext);
    return display_name != NULL && display_name[0] != '\0' ? display_name : NULL;
}

/**
 * @brief 按产品清单声明激活指定 Home 单元。
 * @param[in] unit 待激活的 Home 单元。
 * @return 动作执行成功返回 `true`，否则返回 `false`。
 */
bool home_unit_activate(const app_home_unit_t* unit) {
    product_app_home_action_t action;

    if (unit == NULL || unit->name == NULL || unit->name[0] == '\0') {
        return false;
    }

    action = product_app_get_home_action(unit->name);
    switch (action) {
        case PRODUCT_APP_HOME_ACTION_SET_VIEW:
            return app_router_set_app(unit->name, APP_ROUTER_ENTRY_LOCAL);
        case PRODUCT_APP_HOME_ACTION_OPEN_ASSISTANT:
            return assistant_open(true);
        default:
            floatair_warn("unsupported home action: app=%s action=%d",
                          unit->name,
                          (int)action);
            return false;
    }
}

/**
 * @brief 按系统配置顺序生成去重后的受支持 Home 单元列表。
 * @param[out] out_units 返回动态分配的单元数组。
 * @param[out] out_count 返回有效单元数量。
 * @return 成功生成非空列表时返回 `true`，否则返回 `false`。
 */
bool home_units_build_from_config(app_home_unit_t** out_units, size_t* out_count) {
    size_t configured_count = system_config_get_homeunits_count();
    app_home_unit_t* units = NULL;
    size_t count = 0;

    if (out_units == NULL || out_count == NULL) {
        return false;
    }
    *out_units = NULL;
    *out_count = 0;
    if (configured_count == 0) {
        return false;
    }
    units = (app_home_unit_t*)malloc(sizeof(*units) * configured_count);
    if (units == NULL) {
        floatair_warn("alloc configured home units failed");
        return false;
    }
    for (size_t i = 0; i < configured_count; ++i) {
        const char* configured_name = system_config_get_homeunit(i);
        const app_home_unit_t* unit = home_units_find_supported(configured_name);

        if (unit == NULL) {
            floatair_warn("home unit not supported: %s",
                          configured_name != NULL ? configured_name : "NULL");
            continue;
        }
        if (home_units_contains(units, count, unit->name)) {
            continue;
        }
        units[count++] = *unit;
    }
    if (count == 0) {
        free(units);
        return false;
    }
    *out_units = units;
    *out_count = count;
    return true;
}
