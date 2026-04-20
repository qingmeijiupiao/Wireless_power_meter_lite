#include "web_file.h"

// 声明外部底层的汇编符号
// 因为现在是纯 .c 文件，不需要再套一层 extern "C" {} 了
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// 使用 C 语言的结构体初始化语法 (聚合初始化)
// 注意计算长度时依然减去 1，以剔除 EMBED_TXTFILES 自动追加的 '\0'
const struct EmbeddedFile index_html_file = {
    .data = (const char*)index_html_start,
    .size = (size_t)(index_html_end - index_html_start) - 1
};