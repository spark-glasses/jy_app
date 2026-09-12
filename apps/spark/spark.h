#pragma once

#include "common/app_framework/app_manager.h"
#include "display.h"

#define SPARK_ASSISTANT_MAX_DETAIL 256

typedef enum {
    SPARK_ASSISTANT_IDLE,
    SPARK_ASSISTANT_LISTENING,
    SPARK_ASSISTANT_THINKING,
    SPARK_ASSISTANT_WORKING,
    SPARK_ASSISTANT_NOTES,
    SPARK_ASSISTANT_TODO,
    SPARK_ASSISTANT_CALENDAR,
    SPARK_ASSISTANT_EMAIL,
    SPARK_ASSISTANT_CONTACTS,
    SPARK_ASSISTANT_MAPS,
    SPARK_ASSISTANT_WEB,
    SPARK_ASSISTANT_ERROR,
} spark_assistant_state_t;

typedef struct {
    spark_assistant_state_t state;
    char detail[SPARK_ASSISTANT_MAX_DETAIL + 1];
} spark_assistant_presentation_t;

bool spark_app_register(void);
app_page_t* spark_page_get(void);

const spark_display_t* spark_display_current(void);
// On success, the view owns display. On failure, the caller still owns it.
bool spark_display_apply(spark_display_t* display, bool new_display);
bool spark_display_is_selected(const char* id);
void spark_display_clear(void);
bool spark_reply_set(const char* text);
bool spark_assistant_apply(const spark_assistant_presentation_t* presentation);

bool spark_display_ready(void);
void spark_display_reset_revision(void);
const spark_assistant_presentation_t* spark_assistant_current(void);
bool spark_display_report(const char* command, const char* artifact_id);
// Simulator samples use the same request validation and revision state as the phone.
bool spark_display_preview(mpack_node_t data);
