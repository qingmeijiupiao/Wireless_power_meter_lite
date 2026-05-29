/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 请求 JSON 字段解析工具，基于 nlohmann JSON SAX
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-29
 */
#include "web_backend_internal.h"

#include <cstring>
#include <limits>

#define JSON_NOEXCEPTION 1
#define JSON_NO_IO 1
#include "json.hpp"

namespace WebBackend {

/**
 * @brief handler 关心的字段类型。
 *
 * 这里只支持 Web API 当前需要的少量基础类型，避免引入完整 JSON 对象模型。
 */
enum class JsonFieldType : uint8_t {
    BOOL,
    UINT32,
    STRING,
    EXISTS,
};

/** @brief 单个待提取 JSON 字段的目标描述。 */
struct JsonFieldTarget {
    const char* key;
    JsonFieldType type;
    void* out;
    size_t out_size;
    bool found;
};

/**
 * @brief 只提取指定字段的 SAX 事件接收器。
 *
 * SAX 解析是流式解析：库逐个回调 key/value，而不是构建完整 DOM。
 * 对嵌入式设备来说，这比 `json::parse()` 更节省内存，也减少堆碎片风险。
 */
class JsonFieldSax : public nlohmann::json_sax<nlohmann::json> {
public:
    JsonFieldSax(JsonFieldTarget* targets, size_t target_count)
        : targets_(targets), target_count_(target_count) {}

    bool null() override {
        finish_value();
        return true;
    }

    bool boolean(bool val) override {
        if (pending_ != nullptr && pending_->type == JsonFieldType::BOOL) {
            *static_cast<bool*>(pending_->out) = val;
            pending_->found = true;
        }
        finish_value();
        return true;
    }

    bool number_integer(number_integer_t val) override {
        if (val < 0) {
            finish_value();
            return true;
        }
        return number_unsigned(static_cast<number_unsigned_t>(val));
    }

    bool number_unsigned(number_unsigned_t val) override {
        if (pending_ != nullptr && pending_->type == JsonFieldType::UINT32 &&
            val <= std::numeric_limits<uint32_t>::max()) {
            *static_cast<uint32_t*>(pending_->out) = static_cast<uint32_t>(val);
            pending_->found = true;
        }
        finish_value();
        return true;
    }

    bool number_float(number_float_t, const string_t&) override {
        finish_value();
        return true;
    }

    bool string(string_t& val) override {
        if (pending_ != nullptr && pending_->type == JsonFieldType::STRING &&
            pending_->out != nullptr && pending_->out_size > 0) {
            char* out = static_cast<char*>(pending_->out);
            size_t copy_len = val.size();
            if (copy_len >= pending_->out_size) {
                copy_len = pending_->out_size - 1;
            }
            memcpy(out, val.data(), copy_len);
            out[copy_len] = '\0';
            pending_->found = true;
        }
        finish_value();
        return true;
    }

    bool binary(binary_t&) override {
        finish_value();
        return true;
    }

    bool start_object(std::size_t) override {
        finish_container_start();
        return true;
    }

    bool key(string_t& val) override {
        pending_ = nullptr;
        for (size_t i = 0; i < target_count_; ++i) {
            JsonFieldTarget& target = targets_[i];
            if (target.key != nullptr && strlen(target.key) == val.size() &&
                memcmp(target.key, val.data(), val.size()) == 0) {
                if (target.type == JsonFieldType::EXISTS) {
                    target.found = true;
                }
                pending_ = &target;
                break;
            }
        }
        return true;
    }

    bool end_object() override {
        finish_value();
        return true;
    }

    bool start_array(std::size_t) override {
        finish_container_start();
        return true;
    }

    bool end_array() override {
        finish_value();
        return true;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) override {
        pending_ = nullptr;
        return false;
    }

private:
    /** @brief 目标字段值是对象/数组时，当前工具不展开读取，直接忽略。 */
    void finish_container_start() {
        pending_ = nullptr;
    }

    void finish_value() {
        pending_ = nullptr;
    }

    JsonFieldTarget* targets_;
    size_t target_count_;
    JsonFieldTarget* pending_ = nullptr;
};

/** @brief 执行一次 SAX 解析并填充 targets 中命中的字段。 */
static bool json_read_fields(const char* json, JsonFieldTarget* targets, size_t target_count) {
    if (json == nullptr || targets == nullptr || target_count == 0) {
        return false;
    }
    JsonFieldSax sax(targets, target_count);
    const char* end = json + strlen(json);
    return nlohmann::json::sax_parse(json, end, &sax);
}

/**
 * @brief 读取字符串字段。
 *
 * 字符串会复制到调用方传入的固定缓冲区并保证以 '\0' 结尾。
 */
bool json_get_string(const char* json, const char* key, char* out, size_t out_size) {
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0) {
        return false;
    }
    JsonFieldTarget target = {key, JsonFieldType::STRING, out, out_size, false};
    return json_read_fields(json, &target, 1) && target.found;
}

/** @brief 读取布尔字段，只接受 JSON true/false。 */
bool json_get_bool(const char* json, const char* key, bool* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    JsonFieldTarget target = {key, JsonFieldType::BOOL, out, 0, false};
    return json_read_fields(json, &target, 1) && target.found;
}

/** @brief 读取无符号整数字段，超出 uint32_t 范围时视为未命中。 */
bool json_get_uint32(const char* json, const char* key, uint32_t* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    JsonFieldTarget target = {key, JsonFieldType::UINT32, out, 0, false};
    return json_read_fields(json, &target, 1) && target.found;
}

/** @brief 判断字段是否存在，字段值类型不限。 */
bool json_has_key(const char* json, const char* key) {
    if (json == nullptr || key == nullptr) {
        return false;
    }
    JsonFieldTarget target = {key, JsonFieldType::EXISTS, nullptr, 0, false};
    return json_read_fields(json, &target, 1) && target.found;
}


} // namespace WebBackend
