#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "Policy.h"
#include "Types.h"

struct Route;

class Router {
public:
    uint32_t asn        = 0;
    int32_t  remaining_ = 0;

    std::vector<Router*> providers;
    std::vector<Router*> customers;
    std::vector<Router*> peers;

    std::unique_ptr<Policy> policy;

    Router() = default;
    Router(uint32_t asn, bool use_rov);

    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&)                 = default;
    Router& operator=(Router&&)      = default;

    void forward(std::vector<Router*>& neighbors,
                 Relationship rel,
                 std::vector<std::unique_ptr<Route>>& route_pool);
};
