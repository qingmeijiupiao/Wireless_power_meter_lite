#ifndef BOOT_DIAGNOSTICS_H
#define BOOT_DIAGNOSTICS_H

#include "esp_err.h"

namespace BootDiagnostics {

void append_stage(const char* stage);
void append_hardware_config_failure(esp_err_t err);
void append_system_boot_start();
void append_early();
void append_runtime();

} // namespace BootDiagnostics

#endif
