#include "espnow_pairing_internal.h"

#include <cstring>

namespace EspNowLink::Internal {
namespace {

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_u32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

bool decode_nonce(const EspNowLink::Message& message, uint32_t* nonce) {
    if (nonce == nullptr || message.payload_size != 4) {
        return false;
    }
    *nonce = read_u32(message.payload);
    return true;
}

size_t encode_nonce(uint32_t nonce, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < 4) {
        return 0;
    }
    write_u32(output, nonce);
    return 4;
}

size_t encode_discovery_response(const EspNowLink::MacAddress& target,
                                 uint32_t nonce,
                                 uint8_t* output,
                                 size_t capacity) {
    if (output == nullptr || capacity < EspNowLink::MAC_ADDRESS_SIZE + 4) {
        return 0;
    }
    memcpy(output, target.bytes, EspNowLink::MAC_ADDRESS_SIZE);
    write_u32(output + EspNowLink::MAC_ADDRESS_SIZE, nonce);
    return EspNowLink::MAC_ADDRESS_SIZE + 4;
}

bool decode_discovery_response(const EspNowLink::Message& message,
                               const EspNowLink::MacAddress& local,
                               uint32_t* nonce) {
    if (nonce == nullptr ||
        message.payload_size != EspNowLink::MAC_ADDRESS_SIZE + 4 ||
        memcmp(message.payload, local.bytes, EspNowLink::MAC_ADDRESS_SIZE) != 0) {
        return false;
    }
    *nonce = read_u32(message.payload + EspNowLink::MAC_ADDRESS_SIZE);
    return true;
}

size_t encode_pair_response(uint32_t nonce,
                            const uint8_t lmk[EspNowLink::KEY_SIZE],
                            uint8_t* output,
                            size_t capacity) {
    if (output == nullptr || lmk == nullptr || capacity < 4 + EspNowLink::KEY_SIZE) {
        return 0;
    }
    write_u32(output, nonce);
    memcpy(output + 4, lmk, EspNowLink::KEY_SIZE);
    return 4 + EspNowLink::KEY_SIZE;
}

bool decode_pair_response(const EspNowLink::Message& message,
                          uint32_t* nonce,
                          uint8_t lmk[EspNowLink::KEY_SIZE]) {
    if (nonce == nullptr || lmk == nullptr ||
        message.payload_size != 4 + EspNowLink::KEY_SIZE) {
        return false;
    }
    *nonce = read_u32(message.payload);
    memcpy(lmk, message.payload + 4, EspNowLink::KEY_SIZE);
    return true;
}

} // namespace EspNowLink::Internal
