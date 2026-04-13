#include "Network.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

Router& Network::get_or_create(uint32_t asn) {
    auto [it, inserted] = routers_.try_emplace(asn);
    if (inserted) {
        bool use_rov = rov_set_.count(asn) > 0;
        it->second = Router(asn, use_rov);
    }
    return it->second;
}

void Network::parse_clique(const std::string& line) {
    const std::string tag = "# input clique:";
    std::istringstream iss(line.substr(tag.size()));
    uint32_t asn;
    while (iss >> asn)
        topology_[0].push_back(&get_or_create(asn));
}

int Network::flatten() {
    bool     have_clique     = !topology_[0].empty();
    uint32_t nodes_processed = 0;
    uint32_t total           = static_cast<uint32_t>(routers_.size());

    if (have_clique) {
        for (auto& [asn, r] : routers_)
            r.remaining_ = static_cast<int32_t>(r.providers.size());
        for (Router* r : topology_[0])
            r->remaining_ = 0;

        for (size_t rank = 0; rank < topology_.size(); rank++) {
            for (Router* r : topology_[rank]) {
                nodes_processed++;
                for (Router* customer : r->customers) {
                    if (--customer->remaining_ == 0) {
                        if (topology_.size() <= rank + 1)
                            topology_.emplace_back();
                        topology_[rank + 1].push_back(customer);
                    }
                }
            }
        }
        std::reverse(topology_.begin(), topology_.end());
    } else {
        for (auto& [asn, r] : routers_) {
            r.remaining_ = static_cast<int32_t>(r.customers.size());
            if (r.remaining_ == 0)
                topology_[0].push_back(&r);
        }
        for (size_t rank = 0; rank < topology_.size(); rank++) {
            for (Router* r : topology_[rank]) {
                nodes_processed++;
                for (Router* provider : r->providers) {
                    if (--provider->remaining_ == 0) {
                        if (topology_.size() <= rank + 1)
                            topology_.emplace_back();
                        topology_[rank + 1].push_back(provider);
                    }
                }
            }
        }
    }

    if (nodes_processed < total) {
        std::cerr << "Error: Cycle detected — processed "
                  << nodes_processed << " of " << total << " routers.\n";
        return 1;
    }
    std::cout << "Topology has " << topology_.size() << " ranks.\n";
    return 0;
}

int Network::load_rov(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open ROV file: " << filepath << "\n";
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        try { rov_set_.insert(static_cast<uint32_t>(std::stoul(line))); }
        catch (...) {}
    }
    std::cout << "Loaded " << rov_set_.size() << " ROV routers.\n";
    return 0;
}

int Network::build(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open relationships file: " << filepath << "\n";
        return 1;
    }

    std::string line;
    const std::string clique_tag = "# input clique:";

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind(clique_tag, 0) == 0) parse_clique(line);
            continue;
        }

        std::istringstream iss(line);
        std::string tok;

        std::getline(iss, tok, '|'); if (tok.empty()) continue;
        uint32_t left = 0;
        try { left = static_cast<uint32_t>(std::stoul(tok)); } catch (...) { continue; }

        std::getline(iss, tok, '|'); if (tok.empty()) continue;
        uint32_t right = 0;
        try { right = static_cast<uint32_t>(std::stoul(tok)); } catch (...) { continue; }

        std::getline(iss, tok, '|'); if (tok.empty()) continue;
        int rel = 0;
        try { rel = std::stoi(tok); } catch (...) { continue; }

        Router& l = get_or_create(left);
        Router& r = get_or_create(right);

        if (rel == -1) {
            l.customers.push_back(&r);
            r.providers.push_back(&l);
        } else {
            l.peers.push_back(&r);
            r.peers.push_back(&l);
        }
    }

    std::cout << "Built network with " << routers_.size() << " routers.\n";
    return flatten();
}

int Network::seed(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open announcements file: " << filepath << "\n";
        return 1;
    }

    std::string line;
    std::getline(file, line); // skip header

    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        std::istringstream iss(line);
        std::string tok;

        std::getline(iss, tok, ','); if (tok.empty()) continue;
        uint32_t asn = 0;
        try { asn = static_cast<uint32_t>(std::stoul(tok)); } catch (...) { continue; }

        std::string prefix;
        std::getline(iss, prefix, ',');
        if (prefix.empty()) continue;

        std::string rov_str;
        std::getline(iss, rov_str, ',');
        bool rov_invalid = (rov_str == "True" || rov_str == "true" || rov_str == "1");

        auto it = routers_.find(asn);
        if (it == routers_.end()) {
            std::cerr << "Warning: ASN " << asn << " not in network, skipping.\n";
            continue;
        }
        it->second.policy->seed(prefix, asn, rov_invalid);
        count++;
    }

    std::cout << "Seeded " << count << " origin routes.\n";
    return 0;
}

int Network::propagate() {
    route_pool_.reserve(routers_.size() * 4);

    std::cout << "Phase 1: propagating UP...\n";
    for (auto& rank : topology_)
        for (Router* r : rank)
            r->forward(r->providers, Relationship::CUSTOMER, route_pool_);

    std::cout << "Phase 2: propagating ACROSS...\n";
    {
        struct PeerSend { Router* sender; std::vector<const Route*> routes; };
        std::vector<PeerSend> batches;
        batches.reserve(routers_.size());

        for (auto& [asn, r] : routers_) {
            if (r.peers.empty() || r.policy->rib().empty()) continue;
            PeerSend ps;
            ps.sender = &r;
            for (auto& [pfx, route] : r.policy->rib())
                ps.routes.push_back(route);
            batches.push_back(std::move(ps));
        }

        for (auto& ps : batches) {
            for (Router* peer : ps.sender->peers) {
                for (const Route* route : ps.routes) {
                    auto fwd = std::make_unique<Route>(*route, peer->asn, Relationship::PEER);
                    const Route* ptr = fwd.get();
                    route_pool_.push_back(std::move(fwd));
                    peer->policy->receive(ptr, peer->asn);
                }
            }
        }
    }

    std::cout << "Phase 3: propagating DOWN...\n";
    for (auto it = topology_.rbegin(); it != topology_.rend(); ++it)
        for (Router* r : *it)
            r->forward(r->customers, Relationship::PROVIDER, route_pool_);

    std::cout << "Propagation done. Routes in pool: " << route_pool_.size() << "\n";
    return 0;
}

int Network::output(const std::string& filepath) {
    std::ofstream out(filepath);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot open output file: " << filepath << "\n";
        return 1;
    }

    out << "asn,prefix,as_path\n";
    for (const auto& [asn, router] : routers_)
        for (const auto& [prefix, route] : router.policy->rib())
            out << asn << "," << prefix << ",\"" << route->format_path() << "\"\n";

    out.close();
    std::cout << "Output written to: " << filepath << "\n";
    return 0;
}
