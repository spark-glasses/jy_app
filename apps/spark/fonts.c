#include "fonts.h"

#include "floatair_dbg.h"
#include "lvgl/src/font/lv_binfont_loader.h"
#include "system/system_res.h"

#include <stdio.h>

#define SPARK_FONT_PATH_FORMAT "A:/romfs/system/font/open_runde_%u.bin"

// Every Spark label uses a resident Open Runde bitmap. The vendor TTF font is
// rasterized at run time and is far slower, so it only supplies missing glyphs.
static const unsigned s_sizes[] = {12, 14, 16, 18};
static lv_font_t* s_fonts[sizeof(s_sizes) / sizeof(s_sizes[0])];

const lv_font_t* spark_font(unsigned size) {
    for (size_t i = 0; i < sizeof(s_sizes) / sizeof(s_sizes[0]); ++i) {
        if (s_sizes[i] != size) continue;
        if (s_fonts[i] == NULL) {
            char path[64];
            snprintf(path, sizeof(path), SPARK_FONT_PATH_FORMAT, size);
            s_fonts[i] = lv_binfont_create(path);
            if (s_fonts[i] != NULL) s_fonts[i]->fallback = get_font_by_size_near(size);
        }
        return s_fonts[i] != NULL ? s_fonts[i] : get_font_by_size_near(size);
    }
    floatair_assert(false, "Spark has no %u px bitmap font", size);
    return get_font_by_size_near(size);
}
