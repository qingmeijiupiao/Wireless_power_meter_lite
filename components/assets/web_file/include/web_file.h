#ifndef WEB_FILE_H
#define WEB_FILE_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 嵌入式文件结构体
 * 用于管理存储在 Flash .rodata 段中的静态资源
 */
struct EmbeddedFile {
    const char* data;
    size_t size;
};

extern const EmbeddedFile index_html_file;
// extern const EmbeddedFile StyleCss;
// extern const EmbeddedFile XtermJs;


#ifdef __cplusplus
}
#endif

#endif // WEB_FILE_H
