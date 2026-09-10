#pragma once

#include "common/app_framework/app_manager.h"
#include "display.h"

bool spark_app_register(void);
app_page_t* spark_page_get(void);

const spark_display_t* spark_display_current(void);
// On success, the view owns display. On failure, the caller still owns it.
bool spark_display_apply(spark_display_t* display, bool new_display);
bool spark_display_is_selected(const char* id);
void spark_display_clear(void);
bool spark_reply_set(const char* text);

bool spark_display_ready(void);
void spark_display_reset_revision(void);
void spark_display_report(const char* command, const char* artifact_id);
// Simulator samples use the same request validation and revision state as the phone.
bool spark_display_preview(mpack_node_t data);
