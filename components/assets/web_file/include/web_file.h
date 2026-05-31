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
extern const EmbeddedFile charts_html_file;
extern const EmbeddedFile control_html_file;
extern const EmbeddedFile status_html_file;
extern const EmbeddedFile logs_html_file;
extern const EmbeddedFile blackbox_html_file;
extern const EmbeddedFile firmware_html_file;
extern const EmbeddedFile provision_html_file;
extern const EmbeddedFile app_css_file;
// extern const EmbeddedFile StyleCss;
// extern const EmbeddedFile XtermJs;


#ifdef __cplusplus
}
#endif

#endif // WEB_FILE_H
