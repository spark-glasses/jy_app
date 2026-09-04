/**
 * @file floatair_run.c
 * @brief 应用运行时入口、系统初始化和消息循环调度实现。
 * @author jytek
 * @version 1.0.0
 * @date 2026-01-31
 * @copyright JYTek
 * @ingroup common
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 导入 LVGL 头文件和自定义配置 */
#include "floatair_dbg.h"
#include "floatair_osal.h"
#include "floatair_def.h"
#include "message.h"
#include "app_lcd.h"
#include "app_def.h"
#include "common/app_framework/app_stereo.h"
#include "elf_common.h"
#include "system/system_timer.h"
#include "mpack.h"
#include "common/app_framework/app_router.h"
#include "system/system_config_json.h"
#include "system/stt_common.h"
#include "system/system.h"

#include <lvgl/lvgl.h>

jyt_section_data_t g_section_data = {0};
bt_info g_bt_info                 = {0};

typedef struct floatair_minute_cb_node {
    floatair_minute_cb_t cb;
    struct floatair_minute_cb_node* next;
} floatair_minute_cb_node_t;

static floatair_minute_cb_node_t* g_minute_cb_head = NULL;

#if !defined(BUILD_NATIVE)
/* Hardware-only: the simulator runs LVGL on a separate thread. Collect display
 * timings without printing inside rendering. This includes flush work and
 * explicit refreshes inside message handlers, not just lv_timer_handler(). */
static struct {
    uint32_t start_us;
    uint32_t total_us;
    uint32_t count;
    bool active;
} s_render_timing;

static void floatair_render_timing_event(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        s_render_timing.start_us = (uint32_t)GetTimeUs();
        s_render_timing.active = true;
    } else if (s_render_timing.active) {
        s_render_timing.total_us += (uint32_t)GetTimeUs() - s_render_timing.start_us;
        s_render_timing.count++;
        s_render_timing.active = false;
    }
}
#endif

/**
 * @brief 查询当前 Q-8 中仍待消费的消息数量。
 * @return 成功返回待消费消息数量，失败返回 -1。
 */
static int floatair_get_app_msg_queue_pending(void) {
#if defined(BUILD_NATIVE)
    return -1;
#else
    static mqd_t q8_handle = (mqd_t)-1;
    struct mq_attr attr = {0};

    if (q8_handle == (mqd_t)-1) {
        char qname[MQ_NAME_LEN] = {0};

        attr.mq_maxmsg = 64;
        attr.mq_msgsize = sizeof(OSAL_MQ_MSG*);
        attr.mq_flags = 0;
        snprintf(qname, sizeof(qname), "/JYT_MQ_%d", MQ_JYT_ELFAPP_DATA_IN);
        q8_handle = mq_open(qname, O_RDWR | O_CREAT, 0666, &attr);
        if (q8_handle == (mqd_t)-1) {
            floatair_warn("open Q-8 for attr failed: %s", strerror(errno));
            return -1;
        }
    }

    if (mq_getattr(q8_handle, &attr) != 0) {
        floatair_warn("get Q-8 attr failed: %s", strerror(errno));
        return -1;
    }
    return (int)attr.mq_curmsgs;
#endif
}

/**
 * @brief 打印 ELF 队列消息的头信息与前缀字节，便于排查消息是否被错误转发。
 * @param[in] tag 日志标签。
 * @param[in] msg ELF 队列消息指针。
 * @return 无返回值。
 */
static void floatair_log_elf_queue_msg_preview(const char* tag, const JYT_ELF_MQ_MSG* msg) {
    char payload_preview[3 * 16 + 1] = {0};
    size_t preview_len = 0;
    size_t pos = 0;

    if (msg == NULL) {
        floatair_err("%s: msg is NULL", tag ? tag : "elf msg");
        return;
    }

    preview_len = msg->payload_len;
    if (preview_len > 16) {
        preview_len = 16;
    }

    for (size_t i = 0; i < preview_len && pos + 4 < sizeof(payload_preview); i++) {
        int written = snprintf(payload_preview + pos,
                               sizeof(payload_preview) - pos,
                               "%02X ",
                               msg->payload[i]);
        if (written <= 0) {
            break;
        }
        pos += (size_t)written;
    }

    if (pos > 0) {
        payload_preview[pos - 1] = '\0';
    }

    floatair_info("%s: msg_type=%u event_type=%u simple=%u payload_len=%u preview[%zu]=%s",
                  tag ? tag : "elf msg",
                  (unsigned)msg->Header.msg_type,
                  (unsigned)msg->Header.event_type,
                  (unsigned)msg->Header.simple_data,
                  (unsigned)msg->payload_len,
                  preview_len,
                  pos > 0 ? payload_preview : "<empty>");
}

void floatair_register_minute_cb(floatair_minute_cb_t cb) {
    if (cb == NULL) {
        floatair_err("floatair_register_minute_cb: cb is NULL");
        return;
    }

    floatair_minute_cb_node_t* p = g_minute_cb_head;
    while (p != NULL) {
        if (p->cb == cb) {
            floatair_err("floatair_register_minute_cb: cb already registered");
            return;
        }
        p = p->next;
    }

    floatair_minute_cb_node_t* node =
        (floatair_minute_cb_node_t*)malloc(sizeof(floatair_minute_cb_node_t));
    if (node == NULL) {
        floatair_err("malloc failed in floatair_register_minute_cb");
        return;
    }

    node->cb = cb;
    node->next = NULL;

    if (g_minute_cb_head == NULL) {
        g_minute_cb_head = node;
    } else {
        p = g_minute_cb_head;
        while (p->next != NULL) {
            p = p->next;
        }
        p->next = node;
    }
}

static void floatair_call_minute_cbs(void) {
    floatair_minute_cb_node_t* node = g_minute_cb_head;
    while (node != NULL) {
        if (node->cb != NULL) {
            node->cb();
        }
        node = node->next;
    }
}

#if !defined(BUILD_NATIVE)
/* Drain limits. After the first message arrives, keep pulling messages that
 * are already queued before the single LVGL pass, so a burst produces one
 * frame instead of one frame per message. Bounded so a continuous stream
 * from the phone cannot starve rendering. */
#define APP_MSG_DRAIN_BUDGET_US 10000U
#define APP_MSG_DRAIN_MAX_MSGS  16U
#endif
/* Cycle cost at or above this is logged; a frame costs ~73 ms today. */
#define APP_UI_CYCLE_SLOW_US    50000U

/* UI-loop load diagnostics: per-second load summary plus a cycle log for every
 * host message with a payload. Off by default; the summary and extra logging
 * cost UI-thread time on every cycle. */
#ifndef APP_UI_LOAD_DIAG
#define APP_UI_LOAD_DIAG 0
#endif

typedef struct {
    bool handle_ret;
    uint8_t msg_type;
    uint16_t event_type;
    uint16_t payload_len;
} app_msg_handle_info_t;

/**
 * @brief 分发并释放一条应用队列消息。
 * @param[in] msg 队列消息，函数返回前释放。
 * @param[out] info 本条消息的类型与处理结果，用于周期日志。
 * @return 无返回值。
 */
static void app_msg_handle_one(OSAL_MQ_MSG* msg, app_msg_handle_info_t* info) {
    JYT_ELF_MQ_MSG* p_que_data = NULL;
    bool handle_ret = false;

    memset(info, 0, sizeof(*info));
    if (msg->header.id == LMID_ELFMSG_WRAP) {
        p_que_data = (JYT_ELF_MQ_MSG*)(msg->pdu.ptr[0]);
        if (p_que_data != NULL) {
            info->msg_type = p_que_data->Header.msg_type;
            info->event_type = p_que_data->Header.event_type;
            info->payload_len = p_que_data->payload_len;
            switch (p_que_data->Header.msg_type) {
                case EMT_HOST_MPACK_MSG: {
                    int q_pending = floatair_get_app_msg_queue_pending();
                    stt_set_flow_queue_pending(q_pending);
                    handle_ret = app_mpack_msg_handle((char*)p_que_data->payload, p_que_data->payload_len);
                    break;
                }
                case EMT_SYSTEM_EVENT:
                    handle_ret = app_system_msg_handle(p_que_data);
                    break;
                case EMT_SYSTEM_EMERG_MSG:
                    handle_ret = app_emerg_msg_handle((char*)p_que_data->payload, p_que_data->payload_len);
                    break;
                case EMT_SYSTEM_EVENT_WITH_PAYLOAD:
                    handle_ret = app_system_msg_handle_payload(p_que_data);
                    break;
                default:
                    floatair_err("----, msg type not support %d", p_que_data->Header.msg_type);
                    break;
            }
        } else {
            floatair_err("p_que_data is NULL");
        }
    } else {
        floatair_warn("unexpected mq msg id=%u size=%u in app_msg_recv",
                      (unsigned)msg->header.id,
                      (unsigned)msg->header.pdu_size);
    }
    if (!handle_ret) {
        if (p_que_data != NULL) {
            floatair_log_elf_queue_msg_preview("handle ret false detail", p_que_data);
        }
        floatair_err("handle ret false");
    }

    /* The wrapped ELF message is heap-allocated by system_manager and
     * passed through the MQ as a raw pointer. Handlers copy what they keep. */
    if (p_que_data != NULL) {
        free(p_que_data);
    }
    OSAL_DELETE_MQ_MSG(msg);
    info->handle_ret = handle_ret;
}

static void app_msg_recv(void) {
#if !defined(BUILD_NATIVE)
    uint32_t next_lvgl_ms = 0;
    uint32_t lvgl_finished_ms = lv_tick_get();
    uint32_t last_lvgl_start_us = 0;
    bool have_last_lvgl_start = false;
#if APP_UI_LOAD_DIAG
    uint32_t diagnostic_start_us = (uint32_t)GetTimeUs();
    uint32_t diagnostic_cycles = 0;
    uint32_t diagnostic_render_us = 0;
    uint32_t diagnostic_renders = 0;
    uint32_t diagnostic_max_cycle_us = 0;
    uint32_t diagnostic_max_gap_us = 0;
    floatair_info("UILoad stage=app_start version=%s", JY_APP_VERSION_STRING);
#endif
#endif
    while (1) {
#if !defined(BUILD_NATIVE)
        int timeout_ms = -1;
        if (!floatair_lcd_is_off() && next_lvgl_ms != LV_NO_TIMER_READY) {
            /* Account for cleanup/logging since LVGL returned its delay.
             * A minimum 1 ms wait yields even when drawing missed a deadline. */
            uint32_t elapsed_ms = lv_tick_elaps(lvgl_finished_ms);
            uint32_t remaining_ms = next_lvgl_ms > elapsed_ms ? next_lvgl_ms - elapsed_ms : 1;
            timeout_ms = (int)LV_MIN(remaining_ms, (uint32_t)INT_MAX);
        }
        uint32_t wait_start_us = (uint32_t)GetTimeUs();
        OSAL_MQ_MSG* msg = OSAL_TIMEOUT_WAITING_MQ_MSG(
            MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL, timeout_ms);
#else
        /* The simulator's main thread already services LVGL timers. */
        OSAL_MQ_MSG* msg = OSAL_WAITING_MQ_MSG(MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL);
        if (msg == NULL) {
            continue;
        }
#endif
        uint32_t cycle_start_us = (uint32_t)GetTimeUs();
        uint32_t msg_count = 0;
        bool refreshed = false;
        /* Type and result of the last message handled this cycle. */
        app_msg_handle_info_t info = {0};
#if !defined(BUILD_NATIVE)
        /* Queue wait is time spent receiving, not the age of the message.
         * next_lvgl_ms is meaningful only when refreshed=1; UINT32_MAX
         * means LVGL has no pending timer. The first handler gap is zero. */
        uint32_t queue_wait_us = cycle_start_us - wait_start_us;
        uint32_t lvgl_gap_us = 0;
        uint32_t lvgl_cost_us = 0;
        uint32_t before_lvgl_us = 0;
        s_render_timing.total_us = 0;
        s_render_timing.count = 0;
#endif
        if (msg != NULL) {
            app_msg_handle_one(msg, &info);
            msg_count = 1;
#if !defined(BUILD_NATIVE)
            /* Input and phone messages are cheap next to a frame, so handle
             * everything already queued first and draw once afterwards. The
             * pending check keeps the zero-timeout receive from blocking
             * regardless of how the OSAL treats a zero timeout. */
            while (msg_count < APP_MSG_DRAIN_MAX_MSGS &&
                   (uint32_t)GetTimeUs() - cycle_start_us < APP_MSG_DRAIN_BUDGET_US &&
                   floatair_get_app_msg_queue_pending() > 0) {
                msg = OSAL_TIMEOUT_WAITING_MQ_MSG(MQ_JYT_ELFAPP_DATA_IN, CPU_SPEED_REQ_FULL, 0);
                if (msg == NULL) {
                    break;
                }
                app_msg_handle_one(msg, &info);
                msg_count++;
            }
#endif
        }

        /* Timer work must run after timeouts and unhandled messages too. */
        if (!floatair_lcd_is_off()) {
#if !defined(BUILD_NATIVE)
            uint32_t lvgl_start_us = (uint32_t)GetTimeUs();
            before_lvgl_us = lvgl_start_us - cycle_start_us;
            if (have_last_lvgl_start) {
                lvgl_gap_us = lvgl_start_us - last_lvgl_start_us;
            }
            last_lvgl_start_us = lvgl_start_us;
            have_last_lvgl_start = true;
            next_lvgl_ms = lv_timer_handler();
            lvgl_finished_ms = lv_tick_get();
            lvgl_cost_us = (uint32_t)GetTimeUs() - lvgl_start_us;
#else
            lv_timer_handler();
#endif
            refreshed = true;
        } else {
#if !defined(BUILD_NATIVE)
            next_lvgl_ms = LV_NO_TIMER_READY;
            have_last_lvgl_start = false;
#endif
        }

        /* Logging is syslog on the UI thread, so only report cycles worth
         * looking at: slow ones and handler failures. APP_UI_LOAD_DIAG adds a
         * per-second load summary and every host message with a payload. */
        uint32_t cycle_cost_us = (uint32_t)GetTimeUs() - cycle_start_us;
        bool log_cycle = (msg_count > 0 && !info.handle_ret) || cycle_cost_us >= APP_UI_CYCLE_SLOW_US;
#if !defined(BUILD_NATIVE) && APP_UI_LOAD_DIAG
        log_cycle = log_cycle || (msg_count > 0 && info.payload_len > 0);
        diagnostic_cycles++;
        diagnostic_render_us += s_render_timing.total_us;
        diagnostic_renders += s_render_timing.count;
        diagnostic_max_cycle_us = LV_MAX(diagnostic_max_cycle_us, cycle_cost_us);
        diagnostic_max_gap_us = LV_MAX(diagnostic_max_gap_us, lvgl_gap_us);
        uint32_t diagnostic_now_us = (uint32_t)GetTimeUs();
        uint32_t diagnostic_elapsed_us = diagnostic_now_us - diagnostic_start_us;
        if (diagnostic_elapsed_us >= 1000000) {
            floatair_info("UILoad stage=app_load window_us=%lu screen_on=%d cycles=%lu renders=%lu render_us=%lu max_cycle_us=%lu max_lvgl_gap_us=%lu app_queue=%d",
                          (unsigned long)diagnostic_elapsed_us, !floatair_lcd_is_off(),
                          (unsigned long)diagnostic_cycles, (unsigned long)diagnostic_renders,
                          (unsigned long)diagnostic_render_us, (unsigned long)diagnostic_max_cycle_us,
                          (unsigned long)diagnostic_max_gap_us, floatair_get_app_msg_queue_pending());
            diagnostic_start_us = diagnostic_now_us;
            diagnostic_cycles = diagnostic_render_us = diagnostic_renders = 0;
            diagnostic_max_cycle_us = diagnostic_max_gap_us = 0;
        }
#endif
        if (log_cycle) {
            floatair_info("app ui cycle source=%s msgs=%lu cost_us=%lu handle_ret=%d refreshed=%d msg_type=%u event_type=%u payload_len=%u"
#if !defined(BUILD_NATIVE)
                          " wait_ms=%d queue_wait_us=%lu before_lvgl_us=%lu lvgl_gap_us=%lu lvgl_us=%lu next_lvgl_ms=%lu render_us=%lu renders=%lu"
#endif
                          , msg_count > 0 ? "message" : "timeout",
                          (unsigned long)msg_count,
                          (unsigned long)cycle_cost_us,
                          info.handle_ret ? 1 : 0,
                          refreshed ? 1 : 0,
                          (unsigned)info.msg_type,
                          (unsigned)info.event_type,
                          (unsigned)info.payload_len
#if !defined(BUILD_NATIVE)
                          , timeout_ms,
                          (unsigned long)queue_wait_us,
                          (unsigned long)before_lvgl_us,
                          (unsigned long)lvgl_gap_us,
                          (unsigned long)lvgl_cost_us,
                          (unsigned long)next_lvgl_ms,
                          (unsigned long)s_render_timing.total_us,
                          (unsigned long)s_render_timing.count
#endif
                          );
        }
    }
}

void floatair_load(void) {
    jyt_get_ft_info(&g_section_data);
    system_dump_jyt_section();

    jyt_get_bt_info(&g_bt_info);
    system_dump_bt_info();

    floatair_register_minute_cb(system_update_time);

    app_msg_init();
    if (!system_cfgfile_load()) {
        //floatair_assert(false, "system_cfgfile_load failed");
        floatair_err("system_cfgfile_load failed, wait for debug");
        return;
    }
    if (!system_font_init()) {
        floatair_assert(false, "system_font_init failed");
    }
    // ---------- black full screen at begining -----------
#if !defined(BUILD_NATIVE)
    lv_display_t* display = lv_display_get_default();
    if (display != NULL) {
        lv_display_add_event_cb(display, floatair_render_timing_event, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(display, floatair_render_timing_event, LV_EVENT_RENDER_READY, NULL);
    }
#endif
    lv_obj_t* p_root = system_init_lvgl_fb();
    floatair_assert(p_root != NULL, "system_init_lvgl_fb failed");
    floatair_lcd_set_brightness(system_config_get_brightness());

    //------------------------------------------------------------
    if (!app_router_init()) {
        floatair_assert(false, "app_router_init failed");
    }
    system_init();
    floatair_info("-------enter load_app_home");
    bool ret = app_router_call_home();
    floatair_assert(ret, "app_router_call_home failed");

    floatair_info("-------enter app msg recv");
    app_msg_recv();
}

void floatair_unload(void) {
    floatair_info("-------enter app deinit");
    system_deinit();
    app_msg_deinit();
    floatair_info("LVGL demo completed");
}

void floatair_lvgl_tick(void) {
#if 0
    lv_timer_handler();

    static uint32_t tick_count = 0;
    static uint32_t sec_count = 0;
    tick_count++;

    const uint32_t ticks_per_sec = SYSTEM_LVGL_SECOND_PERIOD / SYSTEM_LVGL_TICK_PERIOD;
    const uint32_t ticks_per_min = (SYSTEM_LVGL_SECOND_PERIOD * 60u) / SYSTEM_LVGL_TICK_PERIOD;

    if (ticks_per_sec > 0 && (tick_count % ticks_per_sec) == 0) {
        sec_count++;
        floatair_info("lvgl tick %" PRIu32 " second", sec_count);
    }

    if (ticks_per_min > 0 && (tick_count % ticks_per_min) == 0) {
        floatair_lvgl_period_minute();
    }
#else
    floatair_lvgl_period_minute();
#endif
}

void floatair_lvgl_period_minute(void) {
    floatair_info("lvgl period minute");
    floatair_call_minute_cbs();
}
