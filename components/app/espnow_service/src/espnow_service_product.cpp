#include "espnow_service_internal.h"

#include <cstdio>

#include "blackbox_service.h"
#include "energy_meter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "global_state.h"
#include "power_output.h"

namespace EspNowService::Internal {
namespace {

constexpr char TAG[] = "EspNowProduct";

const char* action_name(SwitchAction action) {
    switch (action) {
        case SwitchAction::OFF: return "off";
        case SwitchAction::ON: return "on";
        case SwitchAction::TOGGLE: return "toggle";
        default: return "invalid";
    }
}

void format_mac(const EspNowLink::MacAddress& mac, char* output, size_t size) {
    snprintf(output, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac.bytes[0], mac.bytes[1], mac.bytes[2],
             mac.bytes[3], mac.bytes[4], mac.bytes[5]);
}

SwitchResult map_output_result(PowerOutput::OutputResult result) {
    switch (result) {
        case PowerOutput::OutputResult::OK:
            return SwitchResult::OK;
        case PowerOutput::OutputResult::FAIL_NOT_INIT:
            return SwitchResult::NOT_READY;
        case PowerOutput::OutputResult::FAIL_PROTECT_ACTIVE:
        case PowerOutput::OutputResult::FAIL_COOLDOWN_ACTIVE:
            return SwitchResult::REJECTED;
        default:
            return SwitchResult::INTERNAL_ERROR;
    }
}

SwitchResult handle_switch(const EspNowLink::MacAddress& source,
                           SwitchAction action,
                           bool* output_on,
                           void*) {
    if (output_on == nullptr) {
        return SwitchResult::INTERNAL_ERROR;
    }

    const int64_t started_us = esp_timer_get_time();
    PowerOutput::OutputResult output_result = PowerOutput::OutputResult::FAIL_NOT_INIT;
    switch (action) {
        case SwitchAction::OFF:
            output_result = PowerOutput::off(TAG);
            break;
        case SwitchAction::ON:
            output_result = PowerOutput::on(TAG);
            break;
        case SwitchAction::TOGGLE:
            output_result = PowerOutput::toggle(TAG);
            break;
        default:
            *output_on = PowerOutput::get_state();
            return SwitchResult::INVALID_ACTION;
    }

    *output_on = PowerOutput::get_state();
    const SwitchResult result = map_output_result(output_result);
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    char mac[18] = {};
    format_mac(source, mac, sizeof(mac));
    ESP_LOGI(TAG, "switch peer=%s action=%s result=%u output=%u process_us=%lld",
             mac, action_name(action), static_cast<unsigned>(result),
             *output_on ? 1U : 0U, static_cast<long long>(elapsed_us));
    BlackboxService::append_event(
        "espnow: switch peer=%s action=%s result=%u output=%u process_us=%lld",
        mac, action_name(action), static_cast<unsigned>(result),
        *output_on ? 1U : 0U, static_cast<long long>(elapsed_us));
    return result;
}

bool handle_data(const EspNowLink::MacAddress& source, DeviceData* data, void*) {
    if (data == nullptr) {
        return false;
    }

    const int64_t started_us = esp_timer_get_time();
    const auto& state = get_global_state();
    const EnergyMeter::Snapshot meter = EnergyMeter::snapshot();
    data->voltage_mv = state.voltage_mV;
    data->current_ua = state.current_uA;
    data->board_temperature_centi_c = state.board_temperature;
    data->chip_temperature_centi_c = state.chip_temperature;
    data->charge_uah = meter.charge_uah;
    data->energy_uwh = meter.energy_uwh;
    data->meter_time_ms = meter.meter_time_ms;
    data->status_flags = PowerOutput::get_state() ? DEVICE_STATUS_OUTPUT_ON : 0;

    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    char mac[18] = {};
    format_mac(source, mac, sizeof(mac));
    ESP_LOGI(TAG,
             "data peer=%s voltage_mv=%u current_ua=%ld output=%u process_us=%lld",
             mac, data->voltage_mv, static_cast<long>(data->current_ua),
             (data->status_flags & DEVICE_STATUS_OUTPUT_ON) ? 1U : 0U,
             static_cast<long long>(elapsed_us));
    BlackboxService::append_text_event(
        "espnow: data peer=%s voltage_mv=%u current_ua=%ld output=%u process_us=%lld",
        mac, data->voltage_mv, static_cast<long>(data->current_ua),
        (data->status_flags & DEVICE_STATUS_OUTPUT_ON) ? 1U : 0U,
        static_cast<long long>(elapsed_us));
    return true;
}

} // namespace

void register_product_handlers() {
    set_switch_request_handler(handle_switch);
    set_data_request_handler(handle_data);
    ESP_LOGI(TAG, "controller product handlers registered");
    BlackboxService::append_text_event("espnow: product handlers registered");
}

} // namespace EspNowService::Internal

