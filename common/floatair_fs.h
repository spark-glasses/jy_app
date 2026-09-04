#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Return codes: 0 success, negative for errors */
#define FLOATAIR_FS_OK            0
#define FLOATAIR_FS_ERR_GENERIC  -1
#define FLOATAIR_FS_ERR_PARAM    -2
#define FLOATAIR_FS_ERR_NOENT    -3
#define FLOATAIR_FS_ERR_DENIED   -4
#define FLOATAIR_FS_ERR_IO       -5

/* Open mode flags */
#define FLOATAIR_FS_MODE_RD      0x01
#define FLOATAIR_FS_MODE_WR      0x02
#define FLOATAIR_FS_MODE_CREATE  0x04
#define FLOATAIR_FS_MODE_TRUNC   0x08
#define FLOATAIR_FS_MODE_APPEND  0x10

/* Platform paths */
const char *floatair_fs_get_root_path(void);
const char *floatair_fs_get_system_images_path(void);
const char *floatair_fs_get_system_font_file(void);
const char *floatair_fs_get_system_i18n_path(void);
const char *floatair_fs_get_system_config_file(void);

bool floatair_fs_get_app_images_path(const char* app_name, char* out_path, size_t outsz);
bool floatair_fs_get_app_config_file(const char* app_name, char* out_path, size_t outsz);

typedef struct floatair_dir floatair_dir_t;
typedef struct {
    uint32_t size;
    bool is_dir;
} floatair_stat_t;

/**
 * @brief 文件系统容量统计，单位均为字节。
 */
typedef struct {
    uint32_t total;     ///< 文件系统总容量。
    uint32_t used;      ///< 文件系统已使用容量。
    uint32_t remaining; ///< 当前可用剩余容量。
} floatair_fs_usage_t;

/* File operations */
void *floatair_fs_open(const char *path, uint32_t mode);
int floatair_fs_read(void *handle, void *buf, uint32_t btr, uint32_t *br);
int floatair_fs_write(void *handle, const void *buf, uint32_t btw, uint32_t *bw);
int floatair_fs_seek(void *handle, int32_t offset, int whence);
int floatair_fs_tell(void *handle, uint32_t *pos);

/**
 * @brief 将文件缓冲数据同步到底层文件系统。
 * @param[in] handle 已打开的文件句柄。
 * @return `FLOATAIR_FS_OK` 表示同步完成，其他值表示同步失败。
 */
int floatair_fs_sync(void *handle);
int floatair_fs_close(void *handle);
int floatair_fs_remove(const char *path);
int floatair_fs_rename(const char *old_path, const char *new_path);
int floatair_fs_mkdirs(const char *path);
int floatair_fs_mkdir(const char *path);
int floatair_fs_rmdir(const char *path);
int floatair_fs_is_dir(const char *path, bool *is_dir);
int floatair_fs_stat(const char *path, floatair_stat_t *st);
bool floatair_fs_is_exist(const char *path);

/**
 * @brief 获取指定路径所在文件系统的容量统计。
 * @param[in] path 文件系统内的路径。
 * @param[out] usage 容量统计结果，单位为字节。
 * @return `FLOATAIR_FS_OK` 表示成功，其他值表示查询失败。
 */
int floatair_fs_get_usage(const char *path, floatair_fs_usage_t *usage);

/* Directory operations
 * floatair_fs_dir_read:
 *  - On success, fills namebuf with item name.
 *  - If item is a directory, first char of name may be '/', callers can also check via out_is_dir.
 *  - Returns 0 for success, >0 for end-of-dir, negative for errors.
 */
int floatair_fs_dir_open(const char *path, floatair_dir_t **out_dir);
int floatair_fs_dir_read(floatair_dir_t *dir, char *namebuf, uint32_t buflen, bool *out_is_dir);
int floatair_fs_dir_close(floatair_dir_t *dir);
