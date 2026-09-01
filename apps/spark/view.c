#include "spark.h"

#include "common/widgets/label.h"

static void spark_page_create(lv_obj_t* parent, const app_page_data_t* data) {
    (void)data;
    label_t* label = label_create_from_text(parent, "hello");
    floatair_assert(label != NULL, "Spark label create failed");
    if (label != NULL) {
        lv_obj_center(label_get_obj(label));
    }
}

static app_page_t s_spark_page = {
    .name = "spark",
    .on_create = spark_page_create,
};

app_page_t* spark_page_get(void) {
    return &s_spark_page;
}
