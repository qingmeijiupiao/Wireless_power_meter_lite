#include "web_backend_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace WebBackend {
namespace {

constexpr size_t MAX_RTOS_TASKS = 48;
constexpr size_t RTOS_RESPONSE_SIZE = 8192;
constexpr uint32_t RTOS_SAMPLE_MIN_SECONDS = 1;
constexpr uint32_t RTOS_SAMPLE_MAX_SECONDS = 300;

enum class SampleState : uint8_t {
    IDLE,
    SAMPLING,
    READY,
    ERROR,
};

struct TaskResult {
    char name[configMAX_TASK_NAME_LEN];
    int state;
    unsigned priority;
    double cpu_pct;
    unsigned long long runtime_delta;
    unsigned long long runtime_total;
    unsigned stack_free_min_bytes;
};

portMUX_TYPE stats_mux = portMUX_INITIALIZER_UNLOCKED;
SampleState sample_state = SampleState::IDLE;
uint32_t sample_seconds = 0;
int64_t sample_started_us = 0;
unsigned long long sample_total_delta = 0;
double sample_cpu_used_pct = 0.0;
size_t result_count = 0;
TaskResult results[MAX_RTOS_TASKS];
TaskStatus_t before_tasks[MAX_RTOS_TASKS];
TaskStatus_t after_tasks[MAX_RTOS_TASKS];
char rtos_response_buffer[RTOS_RESPONSE_SIZE];

const char* sample_state_to_str(SampleState state) {
    switch (state) {
        case SampleState::IDLE: return "idle";
        case SampleState::SAMPLING: return "sampling";
        case SampleState::READY: return "ready";
        case SampleState::ERROR: return "error";
        default: return "unknown";
    }
}

bool append_response(size_t* pos, const char* fmt, ...) {
    if (*pos >= sizeof(rtos_response_buffer)) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(rtos_response_buffer + *pos, sizeof(rtos_response_buffer) - *pos, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(rtos_response_buffer) - *pos) {
        rtos_response_buffer[sizeof(rtos_response_buffer) - 1] = '\0';
        return false;
    }
    *pos += static_cast<size_t>(n);
    return true;
}

void set_error_state() {
    taskENTER_CRITICAL(&stats_mux);
    sample_state = SampleState::ERROR;
    result_count = 0;
    taskEXIT_CRITICAL(&stats_mux);
}

void sample_task(void*) {
    configRUN_TIME_COUNTER_TYPE before_total = 0;
    configRUN_TIME_COUNTER_TYPE after_total = 0;
    const UBaseType_t before_count =
        uxTaskGetSystemState(before_tasks, MAX_RTOS_TASKS, &before_total);
    if (before_count == 0) {
        set_error_state();
        vTaskDelete(nullptr);
        return;
    }

    uint32_t seconds = 0;
    taskENTER_CRITICAL(&stats_mux);
    seconds = sample_seconds;
    taskEXIT_CRITICAL(&stats_mux);
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));

    const UBaseType_t after_count =
        uxTaskGetSystemState(after_tasks, MAX_RTOS_TASKS, &after_total);
    if (after_count == 0) {
        set_error_state();
        vTaskDelete(nullptr);
        return;
    }

    const configRUN_TIME_COUNTER_TYPE total_delta = after_total - before_total;
    size_t count = 0;
    double idle_cpu_pct = 0.0;
    for (UBaseType_t i = 0; i < after_count && count < MAX_RTOS_TASKS; ++i) {
        const TaskStatus_t& task = after_tasks[i];
        configRUN_TIME_COUNTER_TYPE runtime_delta = 0;
        for (UBaseType_t j = 0; j < before_count; ++j) {
            if (before_tasks[j].xTaskNumber == task.xTaskNumber) {
                runtime_delta = task.ulRunTimeCounter - before_tasks[j].ulRunTimeCounter;
                break;
            }
        }
        const double cpu_pct = total_delta == 0 ? 0.0 :
            100.0 * static_cast<double>(runtime_delta) / static_cast<double>(total_delta);

        TaskResult& result = results[count++];
        snprintf(result.name, sizeof(result.name), "%s", task.pcTaskName);
        result.state = static_cast<int>(task.eCurrentState);
        result.priority = static_cast<unsigned>(task.uxCurrentPriority);
        result.cpu_pct = cpu_pct;
        result.runtime_delta = static_cast<unsigned long long>(runtime_delta);
        result.runtime_total = static_cast<unsigned long long>(task.ulRunTimeCounter);
        result.stack_free_min_bytes = static_cast<unsigned>(task.usStackHighWaterMark);
        if (strcmp(task.pcTaskName, "IDLE") == 0) {
            idle_cpu_pct = cpu_pct;
        }
    }

    taskENTER_CRITICAL(&stats_mux);
    sample_total_delta = static_cast<unsigned long long>(total_delta);
    sample_cpu_used_pct = 100.0 - idle_cpu_pct;
    result_count = count;
    sample_state = SampleState::READY;
    taskEXIT_CRITICAL(&stats_mux);
    vTaskDelete(nullptr);
}

esp_err_t send_current_state(WebServer::Request* request) {
    taskENTER_CRITICAL(&stats_mux);
    const SampleState state = sample_state;
    const uint32_t seconds = sample_seconds;
    const int64_t started_us = sample_started_us;
    const unsigned long long total_delta = sample_total_delta;
    const double cpu_used_pct = sample_cpu_used_pct;
    const size_t tasks = result_count;
    taskEXIT_CRITICAL(&stats_mux);
    const int64_t elapsed_ms = started_us == 0 ? 0 :
        (esp_timer_get_time() - started_us) / 1000;

    size_t pos = 0;
    bool ok = append_response(&pos,
        "{\"state\":\"%s\",\"sample_seconds\":%u,\"elapsed_ms\":%lld",
        sample_state_to_str(state),
        static_cast<unsigned>(seconds),
        static_cast<long long>(elapsed_ms));
    if (state == SampleState::READY) {
        ok = ok && append_response(&pos,
            ",\"total_delta\":%llu,\"cpu_used_pct\":%.3f,\"tasks\":[",
            total_delta,
            cpu_used_pct);
        for (size_t i = 0; ok && i < tasks; ++i) {
            const TaskResult& task = results[i];
            ok = append_response(&pos,
                "%s{\"name\":\"%s\",\"state\":%d,\"priority\":%u,\"cpu_pct\":%.3f,"
                "\"runtime_delta\":%llu,\"runtime_total\":%llu,\"stack_free_min_bytes\":%u}",
                i == 0 ? "" : ",",
                task.name,
                task.state,
                task.priority,
                task.cpu_pct,
                task.runtime_delta,
                task.runtime_total,
                task.stack_free_min_bytes);
        }
        ok = ok && append_response(&pos, "]");
    }
    ok = ok && append_response(&pos, "}\n");

    if (!ok) {
        snprintf(rtos_response_buffer, sizeof(rtos_response_buffer),
            "{\"state\":\"error\",\"reason\":\"response_too_large\"}\n");
    }
    return WebServer::send_json(request, rtos_response_buffer);
}

} // namespace

esp_err_t rtos_stats_handler(WebServer::Request* request) {
    if (request->method == WebServer::Method::GET) {
        return send_current_state(request);
    }

    esp_err_t ret = WebServer::load_body(request);
    if (ret != ESP_OK) {
        return ret;
    }
    uint32_t seconds = 0;
    if (!json_get_uint32(request->body, "seconds", &seconds) ||
        seconds < RTOS_SAMPLE_MIN_SECONDS || seconds > RTOS_SAMPLE_MAX_SECONDS) {
        return WebServer::send(request, 400, "application/json",
            "{\"ok\":false,\"reason\":\"seconds_must_be_1_to_300\"}\n",
            strlen("{\"ok\":false,\"reason\":\"seconds_must_be_1_to_300\"}\n"));
    }

    taskENTER_CRITICAL(&stats_mux);
    if (sample_state == SampleState::SAMPLING) {
        taskEXIT_CRITICAL(&stats_mux);
        return WebServer::send(request, 409, "application/json",
            "{\"ok\":false,\"reason\":\"sampling_in_progress\"}\n",
            strlen("{\"ok\":false,\"reason\":\"sampling_in_progress\"}\n"));
    }
    sample_seconds = seconds;
    sample_started_us = esp_timer_get_time();
    sample_total_delta = 0;
    sample_cpu_used_pct = 0.0;
    result_count = 0;
    sample_state = SampleState::SAMPLING;
    taskEXIT_CRITICAL(&stats_mux);

    if (xTaskCreate(sample_task, "rtos_web_stats", 4096, nullptr, 2, nullptr) != pdPASS) {
        set_error_state();
        return WebServer::send(request, 500, "application/json",
            "{\"ok\":false,\"reason\":\"task_create_failed\"}\n",
            strlen("{\"ok\":false,\"reason\":\"task_create_failed\"}\n"));
    }
    return send_current_state(request);
}

} // namespace WebBackend
