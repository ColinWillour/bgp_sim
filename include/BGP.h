#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include "Policy.h"
#include "Route.h"

class BGP : public Policy {
protected:
    std::unordered_map<std::string, const Route*> local_rib_;
    std::vector<std::unique_ptr<Route>>           owned_;

public:
    void receive(const Route* route, uint32_t my_asn) override;
    void seed(const std::string& prefix, uint32_t asn, bool rov_invalid) override;
    const std::unordered_map<std::string, const Route*>& rib() const override;
};
