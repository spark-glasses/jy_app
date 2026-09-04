/**
 * @file lv_tjpgd.c
 * @brief 为 LVGL 提供低内存、按 MCU 流式输出的 TJPGD JPEG 解码器。
 */

/*********************
 *      INCLUDES
 *********************/

#include "../../draw/lv_image_decoder_private.h"
#include "../../../lvgl.h"
#if LV_USE_TJPGD

#include "tjpgd.h"
#include "lv_tjpgd.h"
#include "../../misc/lv_fs_private.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define DECODER_NAME                    "TJPGD" /**< 解码器注册名称。 */

#define TJPGD_WORKBUFF_SIZE             4096U /**< TJPGD 推荐的单次解码工作池大小。 */

#if JD_FORMAT != 0
    #error "LVGL TJPGD adapter expects RGB888 MCU output"
#endif

#define TJPGD_COLOR_FORMAT              LV_COLOR_FORMAT_L8 /**< 单色光机使用的原生灰度像素格式。 */
#define TJPGD_PIXEL_SIZE                1U                 /**< L8 单像素字节数。 */

/**********************
 *      TYPEDEFS
 **********************/

/** TJPGD 单次解码上下文，使用单块内存避免多个小分配加重堆碎片。 */
typedef struct {
    JDEC decoder; /**< TJPGD 解码状态。 */
    lv_fs_file_t file; /**< JPEG 输入文件。 */
    lv_draw_buf_t draw_buf; /**< 暴露给 LVGL 的 MCU 输出描述符。 */
    uint32_t workbuf[TJPGD_WORKBUFF_SIZE / sizeof(uint32_t)]; /**< 对齐的 TJPGD 工作池。 */
} tjpgd_decoder_ctx_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header);
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);

static lv_result_t decoder_get_area(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc,
                                    const lv_area_t * full_area, lv_area_t * decoded_area);
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static lv_result_t decoder_get_file_header(lv_fs_file_t * file, lv_image_header_t * header);
static size_t input_func(JDEC * jd, uint8_t * buff, size_t ndata);
static int is_jpg(const uint8_t * raw_data, size_t len);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_tjpgd_init(void)
{
    lv_image_decoder_t * dec = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(dec, decoder_info);
    lv_image_decoder_set_open_cb(dec, decoder_open);
    lv_image_decoder_set_get_area_cb(dec, decoder_get_area);
    lv_image_decoder_set_close_cb(dec, decoder_close);

    dec->name = DECODER_NAME;
}

void lv_tjpgd_deinit(void)
{
    lv_image_decoder_t * dec = NULL;
    while((dec = lv_image_decoder_get_next(dec)) != NULL) {
        if(dec->info_cb == decoder_info) {
            lv_image_decoder_delete(dec);
            break;
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * @brief 扫描 JPEG 标记并从 SOF 段读取图像尺寸，避免为信息探测创建 TJPGD 工作池。
 * @param[in,out] file 已打开的 JPEG 文件。
 * @param[out] header 解析得到的 LVGL 图像头。
 * @return `LV_RESULT_OK` 表示解析成功，否则表示文件不是 TJPGD 支持的 JPEG。
 */
static lv_result_t decoder_get_file_header(lv_fs_file_t * file, lv_image_header_t * header)
{
    uint8_t signature[2] = {0};
    uint8_t length_bytes[2] = {0};
    uint8_t frame_header[6] = {0};
    uint32_t read_count = 0;

    if(file == NULL || header == NULL || lv_fs_seek(file, 0, LV_FS_SEEK_SET) != LV_FS_RES_OK) {
        return LV_RESULT_INVALID;
    }
    if(lv_fs_read(file, signature, sizeof(signature), &read_count) != LV_FS_RES_OK ||
       read_count != sizeof(signature) || signature[0] != 0xFFU || signature[1] != 0xD8U) {
        return LV_RESULT_INVALID;
    }

    while(true) {
        uint8_t marker = 0;
        uint16_t segment_length = 0;
        bool is_sof = false;

        do {
            if(lv_fs_read(file, &marker, 1, &read_count) != LV_FS_RES_OK || read_count != 1U) {
                return LV_RESULT_INVALID;
            }
        } while(marker != 0xFFU);

        do {
            if(lv_fs_read(file, &marker, 1, &read_count) != LV_FS_RES_OK || read_count != 1U) {
                return LV_RESULT_INVALID;
            }
        } while(marker == 0xFFU);

        if(marker == 0x00U) continue;
        if(marker == 0xD8U || marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U)) continue;
        if(marker == 0xD9U || marker == 0xDAU) return LV_RESULT_INVALID;

        if(lv_fs_read(file, length_bytes, sizeof(length_bytes), &read_count) != LV_FS_RES_OK ||
           read_count != sizeof(length_bytes)) {
            return LV_RESULT_INVALID;
        }
        segment_length = (uint16_t)(((uint16_t)length_bytes[0] << 8) | length_bytes[1]);
        if(segment_length < 2U) return LV_RESULT_INVALID;

        switch(marker) {
            case 0xC0U:
                is_sof = true;
                break;
            case 0xC1U:
            case 0xC2U:
            case 0xC3U:
            case 0xC5U:
            case 0xC6U:
            case 0xC7U:
            case 0xC9U:
            case 0xCAU:
            case 0xCBU:
            case 0xCDU:
            case 0xCEU:
            case 0xCFU:
                return LV_RESULT_INVALID;
            default:
                break;
        }

        if(is_sof) {
            uint8_t component[3] = {0};

            if(segment_length < 11U ||
               lv_fs_read(file, frame_header, sizeof(frame_header), &read_count) != LV_FS_RES_OK ||
               read_count != sizeof(frame_header) || frame_header[0] != 8U ||
               (frame_header[5] != 1U && frame_header[5] != 3U) ||
               segment_length != (uint16_t)(8U + 3U * frame_header[5])) {
                return LV_RESULT_INVALID;
            }

            header->h = (uint16_t)(((uint16_t)frame_header[1] << 8) | frame_header[2]);
            header->w = (uint16_t)(((uint16_t)frame_header[3] << 8) | frame_header[4]);
            if(header->w == 0U || header->h == 0U) return LV_RESULT_INVALID;

            for(uint8_t i = 0; i < frame_header[5]; i++) {
                if(lv_fs_read(file, component, sizeof(component), &read_count) != LV_FS_RES_OK ||
                   read_count != sizeof(component) || component[2] > 3U) {
                    return LV_RESULT_INVALID;
                }
                if((i == 0U && component[1] != 0x11U && component[1] != 0x22U && component[1] != 0x21U) ||
                   (i != 0U && component[1] != 0x11U)) {
                    return LV_RESULT_INVALID;
                }
            }

            header->cf = LV_COLOR_FORMAT_RAW;
            header->stride = header->w * TJPGD_PIXEL_SIZE;
            return LV_RESULT_OK;
        }

        {
            uint32_t position = 0;
            uint32_t skip_size = (uint32_t)segment_length - 2U;

            if(lv_fs_tell(file, &position) != LV_FS_RES_OK || UINT32_MAX - position < skip_size ||
               lv_fs_seek(file, position + skip_size, LV_FS_SEEK_SET) != LV_FS_RES_OK) {
                return LV_RESULT_INVALID;
            }
        }
    }
}

static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header)
{
    LV_UNUSED(decoder);

    const void * src = dsc->src;
    lv_image_src_t src_type = dsc->src_type;

    if(src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * img_dsc = src;
        uint8_t * raw_data = (uint8_t *)img_dsc->data;
        const uint32_t raw_data_size = img_dsc->data_size;

        if(is_jpg(raw_data, raw_data_size) == true) {
#if LV_USE_FS_MEMFS
            header->cf = LV_COLOR_FORMAT_RAW;
            header->w = img_dsc->header.w;
            header->h = img_dsc->header.h;
            header->stride = img_dsc->header.w * TJPGD_PIXEL_SIZE;
            return LV_RESULT_OK;
#else
            LV_LOG_WARN("LV_USE_FS_MEMFS needs to enabled to decode from data");
            return LV_RESULT_INVALID;
#endif
        }
    }
    else if(src_type == LV_IMAGE_SRC_FILE) {
        const char * fn = src;
        const char * ext = lv_fs_get_ext(fn);
        if((lv_strcmp(ext, "jpg") == 0) || (lv_strcmp(ext, "jpeg") == 0)) {
            return decoder_get_file_header(&dsc->file, header);
        }
    }
    return LV_RESULT_INVALID;
}

static size_t input_func(JDEC * jd, uint8_t * buff, size_t ndata)
{
    lv_fs_file_t * f = jd->device;
    if(!f) return 0;

    if(buff) {
        uint32_t rn = 0;
        lv_fs_read(f, buff, (uint32_t)ndata, &rn);
        return rn;
    }
    else {
        uint32_t pos;
        lv_fs_tell(f, &pos);
        lv_fs_seek(f, (uint32_t)(ndata + pos),  LV_FS_SEEK_SET);
        return ndata;
    }
    return 0;
}

/**
 * Decode a JPG image and return the decoded data.
 * @param decoder pointer to the decoder
 * @param dsc     pointer to the decoder descriptor
 * @return LV_RESULT_OK: no error; LV_RESULT_INVALID: can't open the image
 */
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);

    tjpgd_decoder_ctx_t * ctx = lv_malloc_zeroed(sizeof(*ctx));
    if(ctx == NULL) {
        LV_LOG_WARN("No memory for TJPGD decoder context");
        return LV_RESULT_INVALID;
    }

    lv_fs_res_t fs_res = LV_FS_RES_INV_PARAM;
    if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
#if LV_USE_FS_MEMFS
        const lv_image_dsc_t * img_dsc = dsc->src;
        if(is_jpg(img_dsc->data, img_dsc->data_size) == true) {
            lv_fs_path_ex_t path;
            lv_fs_make_path_from_buffer(&path, LV_FS_MEMFS_LETTER, img_dsc->data, img_dsc->data_size);
            fs_res = lv_fs_open(&ctx->file, (const char *)&path, LV_FS_MODE_RD);
        }
#else
        LV_LOG_WARN("LV_USE_FS_MEMFS needs to enabled to decode from data");
        lv_free(ctx);
        return LV_RESULT_INVALID;
#endif
    }
    else if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * fn = dsc->src;
        if((lv_strcmp(lv_fs_get_ext(fn), "jpg") == 0) || (lv_strcmp(lv_fs_get_ext(fn), "jpeg") == 0)) {
            fs_res = lv_fs_open(&ctx->file, fn, LV_FS_MODE_RD);
        }
    }

    if(fs_res != LV_FS_RES_OK) {
        LV_LOG_WARN("TJPGD input open failed: %d", fs_res);
        lv_free(ctx);
        return LV_RESULT_INVALID;
    }

    JRESULT rc = jd_prepare(&ctx->decoder, input_func, ctx->workbuf, TJPGD_WORKBUFF_SIZE, &ctx->file);
    if(rc != JDR_OK) {
        LV_LOG_WARN("jd_prepare error: %d", rc);
        lv_fs_close(&ctx->file);
        lv_free(ctx);
        return LV_RESULT_INVALID;
    }

    dsc->user_data = ctx;
    dsc->header.cf = TJPGD_COLOR_FORMAT;
    dsc->header.w = ctx->decoder.width;
    dsc->header.h = ctx->decoder.height;
    dsc->header.stride = ctx->decoder.width * TJPGD_PIXEL_SIZE;

    return LV_RESULT_OK;
}

static lv_result_t decoder_get_area(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc,
                                    const lv_area_t * full_area, lv_area_t * decoded_area)
{
    LV_UNUSED(decoder);
    LV_UNUSED(full_area);

    tjpgd_decoder_ctx_t * ctx = dsc->user_data;
    if(ctx == NULL) return LV_RESULT_INVALID;

    JDEC * jd = &ctx->decoder;
    lv_draw_buf_t * decoded = (void *)dsc->decoded;

    uint32_t  mx, my;
    mx = jd->msx * 8;
    my = jd->msy * 8;         /* Size of the MCU (pixel) */
    if(decoded_area->y1 == LV_COORD_MIN) {
        decoded_area->y1 = 0;
        decoded_area->y2 = my - 1;
        decoded_area->x1 = -((int32_t)mx);
        decoded_area->x2 = -1;
        jd->scale = 0;
        jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;   /* Initialize DC values */
        jd->rst = 0;
        jd->rsc = 0;
        if(decoded == NULL) {
            decoded = &ctx->draw_buf;
            dsc->decoded = decoded;
        }
        else {
            if(lv_fs_seek(jd->device, 0, LV_FS_SEEK_SET) != LV_FS_RES_OK) return LV_RESULT_INVALID;
            JRESULT rc = jd_prepare(jd, input_func, ctx->workbuf, TJPGD_WORKBUFF_SIZE, jd->device);
            if(rc) return LV_RESULT_INVALID;
        }
        decoded->data = jd->workbuf;
        decoded->header = dsc->header;
    }

    decoded_area->x1 += mx;
    decoded_area->x2 += mx;

    if(decoded_area->x1 >= jd->width) {
        decoded_area->x1 = 0;
        decoded_area->x2 = mx - 1;
        decoded_area->y1 += my;
        decoded_area->y2 += my;
    }

    if(decoded_area->x2 >= jd->width) decoded_area->x2 = jd->width - 1;
    if(decoded_area->y2 >= jd->height) decoded_area->y2 = jd->height - 1;

    decoded->header.w = lv_area_get_width(decoded_area);
    decoded->header.h = lv_area_get_height(decoded_area);
    decoded->header.stride = decoded->header.w * TJPGD_PIXEL_SIZE;
    decoded->data_size = decoded->header.stride * decoded->header.h;

    /* Process restart interval if enabled */
    JRESULT rc;
    if(jd->nrst && jd->rst++ == jd->nrst) {
        rc = jd_restart(jd, jd->rsc++);
        if(rc != JDR_OK) return LV_RESULT_INVALID;
        jd->rst = 1;
    }

    /* Load an MCU (decompress huffman coded stream, dequantize and apply IDCT) */
    rc = jd_mcu_load(jd);
    if(rc != JDR_OK) return LV_RESULT_INVALID;

    /* Output the MCU as RGB888 into the shared work buffer. */
    rc = jd_mcu_output(jd, NULL, decoded_area->x1, decoded_area->y1);
    if(rc != JDR_OK) return LV_RESULT_INVALID;

    /* Compact BGR888 to L8 in place so LVGL never needs a three-byte-per-pixel MCU buffer. */
    uint8_t * src = decoded->data;
    uint8_t * dst = decoded->data;
    uint32_t pixel_count = decoded->header.w * decoded->header.h;
    for(uint32_t i = 0; i < pixel_count; i++) {
        uint32_t b = src[0];
        uint32_t g = src[1];
        uint32_t r = src[2];
        *dst++ = (uint8_t)((r * 77U + g * 150U + b * 29U + 128U) >> 8);
        src += 3;
    }

    return LV_RESULT_OK;
}

/**
 * Free the allocated resources
 * @param decoder pointer to the decoder where this function belongs
 * @param dsc pointer to a descriptor which describes this decoding session
 */
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    tjpgd_decoder_ctx_t * ctx = dsc->user_data;
    if(ctx == NULL) return;

    lv_fs_close(&ctx->file);
    lv_free(ctx);
    dsc->user_data = NULL;
    dsc->decoded = NULL;
}

static int is_jpg(const uint8_t * raw_data, size_t len)
{
    const uint8_t jpg_signature[] = {0xFF, 0xD8, 0xFF,  0xE0,  0x00,  0x10, 0x4A,  0x46, 0x49, 0x46};
    if(len < sizeof(jpg_signature)) return false;
    return memcmp(jpg_signature, raw_data, sizeof(jpg_signature)) == 0;
}

#endif /*LV_USE_TJPGD*/
