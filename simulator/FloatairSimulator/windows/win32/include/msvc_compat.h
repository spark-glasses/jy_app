#pragma once

#include <time.h>

#if defined(_MSC_VER) && !defined(__clang__)
#ifndef __attribute__
#define __attribute__(x)
#endif

#ifndef __builtin_expect
#define __builtin_expect(x, y) (x)
#endif

/* MSVC 默认 C 模式不识别 SDK 头文件使用的 C11 `_Static_assert`。 */
#ifndef _Static_assert
#define JY_MSVC_JOIN_IMPL(left, right) left##right ///< 连接静态断言类型名与行号。
#define JY_MSVC_JOIN(left, right) JY_MSVC_JOIN_IMPL(left, right) ///< 展开行号后生成唯一类型名。
#define _Static_assert(condition, message)                                      \
    typedef char JY_MSVC_JOIN(jy_msvc_static_assert_, __LINE__)[(condition) ? 1 : -1]
#endif
#endif
