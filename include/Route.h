#pragma once

#include <string>
#include <cstdint>
#include "Types.h"

struct Route {
    std::string  prefix;
    uint32_t     holder;
    uint32_t     path_length;
    Relationship recv_from;
    bool         rov_invalid;
    const Route* prev;

    Route(const std::string& pfx, uint32_t asn, bool rov_inv)
        : prefix(pfx), holder(asn), path_length(1)
        , recv_from(Relationship::ORIGIN), rov_invalid(rov_inv), prev(nullptr)
    {}

    Route(const Route& from, uint32_t new_holder, Relationship rel)
        : prefix(from.prefix), holder(new_holder)
        , path_length(from.path_length + 1)
        , recv_from(rel), rov_invalid(from.rov_invalid), prev(&from)
    {}

    Route(const Route&)            = delete;
    Route& operator=(const Route&) = delete;
    Route(Route&&)                 = delete;
    Route& operator=(Route&&)      = delete;

    uint32_t neighbor_asn() const {
        return (prev != nullptr) ? prev->holder : holder;
    }

    bool path_contains(uint32_t asn) const {
        const Route* cur = this;
        while (cur != nullptr) {
            if (cur->holder == asn) return true;
            cur = cur->prev;
        }
        return false;
    }

    bool better_than(const Route& other) const {
        if (recv_from != other.recv_from)
            return recv_from < other.recv_from;
        if (path_length != other.path_length)
            return path_length < other.path_length;
        return neighbor_asn() < other.neighbor_asn();
    }

    std::string format_path() const;
};
