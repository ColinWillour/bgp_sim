#include "Router.h"
#include "BGP.h"
#include "ROV.h"
#include "Route.h"

Router::Router(uint32_t asn, bool use_rov)
    : asn(asn), remaining_(0)
    , policy(use_rov
        ? std::unique_ptr<Policy>(std::make_unique<ROV>())
        : std::unique_ptr<Policy>(std::make_unique<BGP>()))
{}

void Router::forward(std::vector<Router*>& neighbors,
                     Relationship rel,
                     std::vector<std::unique_ptr<Route>>& route_pool)
{
    if (neighbors.empty()) return;
    const auto& my_rib = policy->rib();
    if (my_rib.empty()) return;

    for (const auto& [prefix, route] : my_rib) {
        bool should_send = false;
        switch (rel) {
            case Relationship::CUSTOMER:
            case Relationship::PEER:
                should_send = (route->recv_from == Relationship::ORIGIN ||
                               route->recv_from == Relationship::CUSTOMER);
                break;
            case Relationship::PROVIDER:
                should_send = true;
                break;
            default: break;
        }
        if (!should_send) continue;

        for (Router* neighbor : neighbors) {
            auto fwd = std::make_unique<Route>(*route, neighbor->asn, rel);
            const Route* fwd_ptr = fwd.get();
            route_pool.push_back(std::move(fwd));
            neighbor->policy->receive(fwd_ptr, neighbor->asn);
        }
    }
}
