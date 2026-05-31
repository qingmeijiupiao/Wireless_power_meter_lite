/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: WiFi/Web 应用层服务组件实现，负责 NVS 配置、STA/AP 模式切换和配网 DNS 劫持
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-28
 */
#include "wifi_service.h"

#include <cstdio>
#include <cstring>

#include "HXC_NVS.h"
#include "dns_server.h"
#include "esp_check.h"
#include "esp_log.h"
#include "web_server.h"
#include "freertos/semphr.h"
#include "global_state.h"
#include "blackbox_service.h"

namespace WifiService {

static const char* TAG = "WifiService";

// NVS key 长度需小于 16 字节，存储于 HXC 默认命名空间。
static HXC::NVS_DATA<char*> sta_ssid("wifi_ssid", "");
static HXC::NVS_DATA<char*> sta_pass("wifi_pass", "");
static HXC::NVS_DATA<uint8_t> web_boot("web_boot", 1);

// 运行期状态由 WiFiService 统一维护，供 Shell 和 WebBackend 查询。
static bool initialized = false;
static Mode mode = Mode::OFF;
static char ap_ssid[WIFI_SSID_MAX_LEN + 1] = {};
static char last_error[64] = "none";
static SemaphoreHandle_t scan_mutex = nullptr;

static void update_global_state_flags() {
    auto& state = get_global_state();
    state.flags.bits.wifi_service_initialized = initialized;
    state.flags.bits.wifi_enabled = mode != Mode::OFF;
    state.flags.bits.wifi_sta_connected = mode == Mode::STA && WiFiManager::instance().is_connected();
    state.flags.bits.wifi_ap_mode = mode == Mode::AP_PROVISION;
    state.flags.bits.wifi_has_saved_sta = has_saved_sta();
    state.flags.bits.wifi_web_enabled_on_boot = is_web_enabled_on_boot();
}

/**
 * @brief 更新最近一次错误描述
 *
 * @param err 错误字符串，nullptr 时记录为 "unknown"
 */
static void set_last_error(const char* err) {
    strncpy(last_error, err == nullptr ? "unknown" : err, sizeof(last_error) - 1);
    last_error[sizeof(last_error) - 1] = '\0';
}

/**
 * @brief 基于 AP MAC 生成默认配网热点名
 *
 * 命名格式为 `WPM-Lite-XXXXXX`，后缀取 AP MAC 的后三字节，便于用户区分多台设备。
 */
static void make_ap_ssid() {
    MAC_t mac = WiFiManager::instance().get_mac(WIFI_IF_AP);
    snprintf(ap_ssid, sizeof(ap_ssid), "WPM-Lite-%02X%02X%02X", mac.octet4, mac.octet5, mac.octet6);
}

/**
 * @brief 将 AP netif IP 设置为 WiFiService 编译期常量
 */
static esp_err_t configure_ap_ip() {
    IP_t ip = {};
    ip.octet1 = AP_IP_OCTET1;
    ip.octet2 = AP_IP_OCTET2;
    ip.octet3 = AP_IP_OCTET3;
    ip.octet4 = AP_IP_OCTET4;

    IP_t netmask = {};
    netmask.octet1 = 255;
    netmask.octet2 = 255;
    netmask.octet3 = 255;
    netmask.octet4 = 0;

    return WiFiManager::instance().set_ap_ip(ip, netmask);
}

/**
 * @brief 初始化 WiFiService
 */
esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }
    HXC::NVS_Base::setup();
    ESP_RETURN_ON_ERROR(WiFiManager::instance().init(), TAG, "wifi manager init failed");
    make_ap_ssid();
    scan_mutex = xSemaphoreCreateMutex();
    if (scan_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    initialized = true;
    update_global_state_flags();
    BlackboxService::append_event("wifi: init web_boot=%u saved_sta=%u",
                                  is_web_enabled_on_boot() ? 1U : 0U,
                                  has_saved_sta() ? 1U : 0U);
    return ESP_OK;
}

/**
 * @brief 查询初始化状态
 */
bool is_initialized() {
    return initialized;
}

/**
 * @brief 查询启动时是否启用 WiFi/Web
 */
bool is_web_enabled_on_boot() {
    return web_boot.read() != 0;
}

/**
 * @brief 写入启动启用开关
 */
esp_err_t set_web_enabled_on_boot(bool enabled) {
    web_boot = enabled ? 1 : 0;
    update_global_state_flags();
    BlackboxService::append_event("wifi: web_boot=%u", enabled ? 1U : 0U);
    return ESP_OK;
}

/**
 * @brief 清除已保存 STA 凭据
 */
esp_err_t clear_saved_sta() {
    sta_ssid = "";
    sta_pass = "";
    update_global_state_flags();
    BlackboxService::append_event("wifi: saved_sta_cleared");
    return ESP_OK;
}

/**
 * @brief 判断 NVS 中是否存在有效 SSID
 */
bool has_saved_sta() {
    char* ssid = sta_ssid.read();
    return ssid != nullptr && ssid[0] != '\0';
}

/**
 * @brief 读取当前配置快照
 */
Config get_config() {
    Config cfg = {};
    char* ssid = sta_ssid.read();
    char* pass = sta_pass.read();
    if (ssid != nullptr) {
        strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    }
    if (pass != nullptr) {
        strncpy(cfg.password, pass, sizeof(cfg.password) - 1);
    }
    cfg.web_enabled_on_boot = is_web_enabled_on_boot();
    return cfg;
}

/**
 * @brief 连接指定 STA WiFi
 */
esp_err_t connect_sta(const char* ssid, const char* password, bool save) {
    if (ssid == nullptr || ssid[0] == '\0' || password == nullptr) {
        set_last_error("invalid sta config");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(init(), TAG, "init failed");

    // 从配网 AP 切换到 STA 前必须关闭 DNS 劫持和 Captive Portal 回落。
    DNSServer::stop();
    WebServer::enable_captive_portal(false);

    esp_err_t ret = WiFiManager::instance().connect_sta(ssid, password, true);
    if (ret == ESP_OK) {
        mode = Mode::STA;
        update_global_state_flags();
        set_last_error("none");

        // 只有业务层明确要求保存时才写入 NVS，启动自动连接不会重复写 Flash。
        if (save) {
            sta_ssid = ssid;
            sta_pass = password;
        }
        IP_t ip = WiFiManager::instance().get_ip();
        ESP_LOGI(TAG, "STA connected: %u.%u.%u.%u", ip.octet1, ip.octet2, ip.octet3, ip.octet4);
        BlackboxService::append_event("wifi: sta_connected save=%u ip=%u.%u.%u.%u",
                                      save ? 1U : 0U,
                                      ip.octet1,
                                      ip.octet2,
                                      ip.octet3,
                                      ip.octet4);
        return ESP_OK;
    }

    set_last_error(esp_err_to_name(ret));
    ESP_LOGW(TAG, "STA connect failed: %s", esp_err_to_name(ret));
    BlackboxService::append_event("wifi: sta_connect_failed save=%u err=%s",
                                  save ? 1U : 0U,
                                  esp_err_to_name(ret));
    return ret;
}

/**
 * @brief 启动开放 AP 配网模式
 */
esp_err_t start_provision_ap() {
    ESP_RETURN_ON_ERROR(init(), TAG, "init failed");
    if (ap_ssid[0] == '\0') {
        make_ap_ssid();
    }

    // AP 使用空密码，降低首次配网门槛；HTTP 404/Captive 请求回落到配网页。
    ESP_RETURN_ON_ERROR(configure_ap_ip(), TAG, "set ap ip failed");
    ESP_RETURN_ON_ERROR(WiFiManager::instance().start_apsta(ap_ssid, ""), TAG, "start apsta failed");
    ESP_RETURN_ON_ERROR(DNSServer::start(AP_IP_OCTET1, AP_IP_OCTET2, AP_IP_OCTET3, AP_IP_OCTET4), TAG, "start dns failed");
    WebServer::enable_captive_portal(true);
    mode = Mode::AP_PROVISION;
    update_global_state_flags();
    set_last_error("none");
    ESP_LOGI(TAG, "Provision AP active: %s", ap_ssid);
    BlackboxService::append_event("wifi: provision_ap ssid=%s", ap_ssid);
    return ESP_OK;
}

esp_err_t scan_ap_list(ScanResult* results, size_t max_results, size_t* out_count) {
    if (results == nullptr || out_count == nullptr || max_results == 0) {
        set_last_error("invalid scan args");
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    ESP_RETURN_ON_ERROR(init(), TAG, "init failed");

    if (scan_mutex == nullptr || xSemaphoreTake(scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        set_last_error("scan busy");
        return ESP_ERR_TIMEOUT;
    }

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 80;
    scan_cfg.scan_time.active.max = 180;

    esp_err_t ret = WiFiManager::instance().scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        set_last_error(esp_err_to_name(ret));
        xSemaphoreGive(scan_mutex);
        return ret;
    }

    uint16_t ap_num = 0;
    ret = WiFiManager::instance().scan_get_ap_num(&ap_num);
    if (ret != ESP_OK) {
        set_last_error(esp_err_to_name(ret));
        xSemaphoreGive(scan_mutex);
        return ret;
    }

    wifi_ap_record_t records[WIFI_SCAN_MAX_RESULTS] = {};
    uint16_t fetch_num = ap_num > WIFI_SCAN_MAX_RESULTS ? WIFI_SCAN_MAX_RESULTS : ap_num;
    ret = WiFiManager::instance().scan_get_ap_records(&fetch_num, records);
    if (ret != ESP_OK) {
        set_last_error(esp_err_to_name(ret));
        xSemaphoreGive(scan_mutex);
        return ret;
    }

    size_t written = 0;
    for (uint16_t i = 0; i < fetch_num && written < max_results; ++i) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }

        bool duplicated = false;
        for (size_t j = 0; j < written; ++j) {
            if (strncmp(results[j].ssid, reinterpret_cast<const char*>(records[i].ssid), WIFI_SSID_MAX_LEN) == 0) {
                duplicated = true;
                break;
            }
        }
        if (duplicated) {
            continue;
        }

        strncpy(results[written].ssid, reinterpret_cast<const char*>(records[i].ssid), WIFI_SSID_MAX_LEN);
        results[written].ssid[WIFI_SSID_MAX_LEN] = '\0';
        results[written].rssi = records[i].rssi;
        results[written].channel = records[i].primary;
        results[written].authmode = records[i].authmode;
        written++;
    }

    *out_count = written;
    set_last_error("none");
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

/**
 * @brief 按 NVS 配置启动默认模式
 */
esp_err_t start_default() {
    ESP_RETURN_ON_ERROR(init(), TAG, "init failed");

    if (!is_web_enabled_on_boot()) {
        ESP_LOGI(TAG, "web/wifi startup disabled by NVS");
        mode = Mode::OFF;
        update_global_state_flags();
        BlackboxService::append_event("wifi: startup_disabled");
        return ESP_OK;
    }

    Config cfg = get_config();
    if (cfg.ssid[0] != '\0') {
        esp_err_t ret = connect_sta(cfg.ssid, cfg.password, false);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
    }

    // 没有保存凭据，或保存的凭据连接超时/失败时，进入 AP 配网兜底模式。
    return start_provision_ap();
}

/**
 * @brief 停止 WiFiService 管理的网络功能
 */
esp_err_t stop() {
    DNSServer::stop();
    WebServer::enable_captive_portal(false);
    esp_err_t ret = WiFiManager::instance().stop();
    mode = Mode::OFF;
    update_global_state_flags();
    BlackboxService::append_event("wifi: stop result=%s", esp_err_to_name(ret));
    return ret;
}

/**
 * @brief 查询是否处于 AP 配网模式
 */
bool is_provisioning() {
    return mode == Mode::AP_PROVISION;
}

/**
 * @brief 获取当前模式
 */
Mode get_mode() {
    return mode;
}

/**
 * @brief 获取配网热点名
 */
const char* get_ap_ssid() {
    if (ap_ssid[0] == '\0') {
        make_ap_ssid();
    }
    return ap_ssid;
}

/**
 * @brief 获取最近错误描述
 */
const char* get_last_error() {
    return last_error;
}

/**
 * @brief 获取当前对外访问 IP
 */
IP_t get_ip() {
    if (mode == Mode::AP_PROVISION) {
        IP_t ip = {};
        ip.octet1 = AP_IP_OCTET1;
        ip.octet2 = AP_IP_OCTET2;
        ip.octet3 = AP_IP_OCTET3;
        ip.octet4 = AP_IP_OCTET4;
        return ip;
    }
    return WiFiManager::instance().get_ip();
}

/**
 * @brief 获取底层 WiFi 状态
 */
wifi_state_t get_wifi_state() {
    return WiFiManager::instance().get_state();
}

/**
 * @brief 获取当前 STA 连接的 RSSI
 */
int8_t get_rssi() {
    return WiFiManager::instance().get_rssi();
}

/**
 * @brief 将当前 STA RSSI 映射为便于展示的信号强度百分比
 */
uint8_t get_signal_percent() {
    if (get_wifi_state() != WIFI_STATE_STA_CONNECTED) {
        return 0;
    }

    int8_t rssi = get_rssi();
    if (rssi <= -100) {
        return 0;
    }
    if (rssi >= -50) {
        return 100;
    }
    return static_cast<uint8_t>(2 * (rssi + 100));
}

/**
 * @brief 获取当前 WiFi 主信道
 */
esp_err_t get_channel(uint8_t* channel) {
    if (channel == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    return WiFiManager::instance().get_channel(channel, &second);
}

} // namespace WifiService
