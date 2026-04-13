#pragma once

#include <cstdint>

enum class Relationship : uint8_t {
    ORIGIN   = 0,
    CUSTOMER = 1,
    PEER     = 2,
    PROVIDER = 3
};

static constexpr uint32_t MAX_ASN = 4294967295u;
