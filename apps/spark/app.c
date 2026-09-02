#include "spark.h"

#include "app_def.h"
#include "common/app_framework/app_nav.h"
#include "system/system.h"

static void spark_app_on_start(void) {
    system_status_bar_set_mode(true);
    if (!app_nav_replace(spark_page_get(), NULL, 0)) {
        floatair_assert(false, "Spark page replace failed");
    }
}

static app_t s_spark_app = {
    // Keep setup, reset, and return-home routes pointed at Spark.
    .name = APP_NAME_HOME,
    .on_start = spark_app_on_start,
};

bool spark_app_register(void) {
    return app_manager_register(&s_spark_app);
}
