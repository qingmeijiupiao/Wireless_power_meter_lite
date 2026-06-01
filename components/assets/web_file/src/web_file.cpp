#include "web_file.h"

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t charts_html_gz_start[] asm("_binary_charts_html_gz_start");
extern const uint8_t charts_html_gz_end[] asm("_binary_charts_html_gz_end");
extern const uint8_t control_html_gz_start[] asm("_binary_control_html_gz_start");
extern const uint8_t control_html_gz_end[] asm("_binary_control_html_gz_end");
extern const uint8_t status_html_gz_start[] asm("_binary_status_html_gz_start");
extern const uint8_t status_html_gz_end[] asm("_binary_status_html_gz_end");
extern const uint8_t logs_html_gz_start[] asm("_binary_logs_html_gz_start");
extern const uint8_t logs_html_gz_end[] asm("_binary_logs_html_gz_end");
extern const uint8_t blackbox_html_gz_start[] asm("_binary_blackbox_html_gz_start");
extern const uint8_t blackbox_html_gz_end[] asm("_binary_blackbox_html_gz_end");
extern const uint8_t firmware_html_gz_start[] asm("_binary_firmware_html_gz_start");
extern const uint8_t firmware_html_gz_end[] asm("_binary_firmware_html_gz_end");
extern const uint8_t provision_html_gz_start[] asm("_binary_provision_html_gz_start");
extern const uint8_t provision_html_gz_end[] asm("_binary_provision_html_gz_end");
extern const uint8_t app_css_gz_start[] asm("_binary_app_css_gz_start");
extern const uint8_t app_css_gz_end[] asm("_binary_app_css_gz_end");

const EmbeddedFile index_html_file = {
    .data = reinterpret_cast<const char*>(index_html_gz_start),
    .size = static_cast<size_t>(index_html_gz_end - index_html_gz_start),
};

const EmbeddedFile charts_html_file = {
    .data = reinterpret_cast<const char*>(charts_html_gz_start),
    .size = static_cast<size_t>(charts_html_gz_end - charts_html_gz_start),
};

const EmbeddedFile control_html_file = {
    .data = reinterpret_cast<const char*>(control_html_gz_start),
    .size = static_cast<size_t>(control_html_gz_end - control_html_gz_start),
};

const EmbeddedFile status_html_file = {
    .data = reinterpret_cast<const char*>(status_html_gz_start),
    .size = static_cast<size_t>(status_html_gz_end - status_html_gz_start),
};

const EmbeddedFile logs_html_file = {
    .data = reinterpret_cast<const char*>(logs_html_gz_start),
    .size = static_cast<size_t>(logs_html_gz_end - logs_html_gz_start),
};

const EmbeddedFile blackbox_html_file = {
    .data = reinterpret_cast<const char*>(blackbox_html_gz_start),
    .size = static_cast<size_t>(blackbox_html_gz_end - blackbox_html_gz_start),
};

const EmbeddedFile firmware_html_file = {
    .data = reinterpret_cast<const char*>(firmware_html_gz_start),
    .size = static_cast<size_t>(firmware_html_gz_end - firmware_html_gz_start),
};

const EmbeddedFile provision_html_file = {
    .data = reinterpret_cast<const char*>(provision_html_gz_start),
    .size = static_cast<size_t>(provision_html_gz_end - provision_html_gz_start),
};

const EmbeddedFile app_css_file = {
    .data = reinterpret_cast<const char*>(app_css_gz_start),
    .size = static_cast<size_t>(app_css_gz_end - app_css_gz_start),
};
