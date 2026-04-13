#pragma once

#include <string>
#include <unordered_map>
#include "Route.h"

class Policy {
public:
    virtual void receive(const Route* route, uint32_t my_asn) = 0;
    virtual void seed(const std::string& prefix, uint32_t asn, bool rov_invalid) = 0;
    virtual const std::unordered_map<std::string, const Route*>& rib() const = 0;
    virtual ~Policy() = default;
};
