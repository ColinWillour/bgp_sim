#include "BGP.h"

void BGP::seed(const std::string& prefix, uint32_t asn, bool rov_invalid) {
    if (local_rib_.count(prefix)) return;
    auto route = std::make_unique<Route>(prefix, asn, rov_invalid);
    const Route* ptr = route.get();
    owned_.push_back(std::move(route));
    local_rib_[prefix] = ptr;
}

void BGP::receive(const Route* route, uint32_t my_asn) {
    if (route->prev && route->prev->path_contains(my_asn)) return;
    if (!route->prev && route->holder == my_asn) return;

    auto it = local_rib_.find(route->prefix);
    if (it == local_rib_.end()) {
        local_rib_[route->prefix] = route;
    } else if (route->better_than(*it->second)) {
        local_rib_[route->prefix] = route;
    }
}

const std::unordered_map<std::string, const Route*>& BGP::rib() const {
    return local_rib_;
}
