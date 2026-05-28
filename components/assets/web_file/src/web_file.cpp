#include "web_file.h"

// 声明外部底层的汇编符号
// 因为现在是纯 .c 文件，不需要再套一层 extern "C" {} 了
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t charts_html_start[] asm("_binary_charts_html_start");
extern const uint8_t charts_html_end[]   asm("_binary_charts_html_end");
extern const uint8_t control_html_start[] asm("_binary_control_html_start");
extern const uint8_t control_html_end[]   asm("_binary_control_html_end");
extern const uint8_t status_html_start[] asm("_binary_status_html_start");
extern const uint8_t status_html_end[]   asm("_binary_status_html_end");
extern const uint8_t logs_html_start[] asm("_binary_logs_html_start");
extern const uint8_t logs_html_end[]   asm("_binary_logs_html_end");
extern const uint8_t blackbox_html_start[] asm("_binary_blackbox_html_start");
extern const uint8_t blackbox_html_end[]   asm("_binary_blackbox_html_end");
extern const uint8_t provision_html_start[] asm("_binary_provision_html_start");
extern const uint8_t provision_html_end[]   asm("_binary_provision_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[]   asm("_binary_app_css_end");

// 使用 C 语言的结构体初始化语法 (聚合初始化)
// 注意计算长度时依然减去 1，以剔除 EMBED_TXTFILES 自动追加的 '\0'
const struct EmbeddedFile index_html_file = {
    .data = (const char*)index_html_start,
    .size = (size_t)(index_html_end - index_html_start) - 1
};

const struct EmbeddedFile charts_html_file = {
    .data = (const char*)charts_html_start,
    .size = (size_t)(charts_html_end - charts_html_start) - 1
};

const struct EmbeddedFile control_html_file = {
    .data = (const char*)control_html_start,
    .size = (size_t)(control_html_end - control_html_start) - 1
};

const struct EmbeddedFile status_html_file = {
    .data = (const char*)status_html_start,
    .size = (size_t)(status_html_end - status_html_start) - 1
};

const struct EmbeddedFile logs_html_file = {
    .data = (const char*)logs_html_start,
    .size = (size_t)(logs_html_end - logs_html_start) - 1
};

const struct EmbeddedFile blackbox_html_file = {
    .data = (const char*)blackbox_html_start,
    .size = (size_t)(blackbox_html_end - blackbox_html_start) - 1
};

const struct EmbeddedFile provision_html_file = {
    .data = (const char*)provision_html_start,
    .size = (size_t)(provision_html_end - provision_html_start) - 1
};

const struct EmbeddedFile app_css_file = {
    .data = (const char*)app_css_start,
    .size = (size_t)(app_css_end - app_css_start) - 1
};
