#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>
#include "Router.h"
#include "Route.h"

class Network {
    std::unordered_map<uint32_t, Router>      routers_;
    std::vector<std::vector<Router*>>          topology_;
    std::vector<std::unique_ptr<Route>>        route_pool_;
    std::unordered_set<uint32_t>              rov_set_;

    Router& get_or_create(uint32_t asn);
    void    parse_clique(const std::string& line);
    int     flatten();

public:
    Network() {
        routers_.reserve(90000);
        topology_.emplace_back();
    }

    int load_rov(const std::string& filepath);
    int build(const std::string& filepath);
    int seed(const std::string& filepath);
    int propagate();
    int output(const std::string& filepath);
};
