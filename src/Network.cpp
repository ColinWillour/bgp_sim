#include "Network.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// ─── Internal helpers ────────────────────────────────────────────────────────

Router& Network::get_or_create(uint32_t asn) {
    auto [it, inserted] = routers_.try_emplace(asn);
    if (inserted) {
        bool use_rov = rov_set_.count(asn) > 0;
        it->second = Router(asn, use_rov);
    }
    return it->second;
}

void Network::parse_clique(const std::string& line) {
    // "# input clique: 174 209 286 701 ..."
    const std::string tag = "# input clique:";
    std::istringstream iss(line.substr(tag.size()));
    uint32_t asn;
    while (iss >> asn) {
        topology_[0].push_back(&get_or_create(asn));
    }
}

int Network::flatten() {
    // Kahn's algorithm — bottom-up BFS from leaves to clique.
    //
    // remaining_ counts how many customers each router is still waiting on.
    // Routers with remaining_==0 (no customers, or all customers processed)
    // are leaves and get rank 0.
    //
    // If we have a known clique, we force those nodes to remaining_=0
    // so they are treated as the top tier regardless of what the file says
    // about relationships between clique members.
    //
    // Cycle detection: if nodes_processed < total, some nodes were never
    // reached — they are in a provider/customer cycle.

    uint32_t total = static_cast<uint32_t>(routers_.size());

    // Build clique set for O(1) lookup
    std::unordered_set<uint32_t> clique_set;
    for (Router* r : topology_[0])
        clique_set.insert(r->asn);

    // Initialize remaining_ = number of customers, but ignore
    // customer relationships between two clique members.
    for (auto& [asn, r] : routers_) {
        if (clique_set.count(asn)) {
            r.remaining_ = 0;  // clique nodes start ready
        } else {
            int32_t cnt = 0;
            for (Router* c : r.customers) {
                if (!clique_set.count(c->asn)) cnt++;
            }
            r.remaining_ = cnt;
        }
    }

    // Seed rank 0 with leaves (remaining_==0)
    topology_[0].clear();
    for (auto& [asn, r] : routers_) {
        if (r.remaining_ == 0)
            topology_[0].push_back(&r);
    }

    uint32_t nodes_processed = 0;
    for (size_t rank = 0; rank < topology_.size(); rank++) {
        for (Router* r : topology_[rank]) {
            nodes_processed++;
            for (Router* provider : r->providers) {
                if (clique_set.count(provider->asn)) continue; // clique already seeded
                if (--provider->remaining_ == 0) {
                    if (topology_.size() <= rank + 1)
                        topology_.emplace_back();
                    topology_[rank + 1].push_back(provider);
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

// ─── Public interface ────────────────────────────────────────────────────────

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
    std::vector<uint32_t> clique_asns;  // store clique ASNs, populate topology_ after build

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        if (line[0] == '#') {
            if (line.rfind(clique_tag, 0) == 0) {
                // Store clique ASNs for later — don't touch topology_ yet
                std::istringstream iss(line.substr(clique_tag.size()));
                uint32_t asn;
                while (iss >> asn) clique_asns.push_back(asn);
            }
            continue;
        }

        // Format: left_asn|right_asn|rel|source
        // We only need the first three tokens.
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

        // source field (bgp / mlp / bgp,mlp) — ignored per spec

        Router& l = get_or_create(left);
        Router& r = get_or_create(right);

        if (rel == -1) {
            // left is provider, right is customer
            l.customers.push_back(&r);
            r.providers.push_back(&l);
        } else {
            // rel == 0: peer (bidirectional)
            l.peers.push_back(&r);
            r.peers.push_back(&l);
        }
    }

    // Now that routers_ is fully built and will never rehash,
    // populate topology_[0] with stable pointers to clique nodes.
    for (uint32_t asn : clique_asns) {
        auto it = routers_.find(asn);
        if (it != routers_.end())
            topology_[0].push_back(&it->second);
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
    std::getline(file, line);  // skip header

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

    // ── Phase 1: UP (leaves → clique) ────────────────────────────────────────
    // Each router sends its RIB to its providers.
    // Providers receive routes tagged as CUSTOMER
    // (from the provider's view: this came from a customer).
    std::cout << "Phase 1: propagating UP...\n";
    for (auto& rank : topology_) {
        for (Router* r : rank) {
            r->forward(r->providers, Relationship::CUSTOMER, route_pool_);
        }
    }

    // ── Phase 2: ACROSS (peers, one hop only) ────────────────────────────────
    // All routers send to peers simultaneously, THEN all process.
    // This two-pass approach prevents announcements hopping multiple peers
    // in a single phase (which would violate valley-free routing).
    std::cout << "Phase 2: propagating ACROSS...\n";
    {
        // Snapshot every router's RIB before any peer receiving happens
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

        // Deliver all peer routes after snapshot is complete
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

    // ── Phase 3: DOWN (clique → leaves) ──────────────────────────────────────
    // Each router sends its RIB to its customers.
    // Customers receive routes tagged as PROVIDER
    // (from the customer's view: this came from a provider).
    std::cout << "Phase 3: propagating DOWN...\n";
    for (auto it = topology_.rbegin(); it != topology_.rend(); ++it) {
        for (Router* r : *it) {
            r->forward(r->customers, Relationship::PROVIDER, route_pool_);
        }
    }

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
    for (const auto& [asn, router] : routers_) {
        for (const auto& [prefix, route] : router.policy->rib()) {
            out << asn << ","
                << prefix << ",\""
                << route->format_path() << "\"\n";
        }
    }

    out.close();
    std::cout << "Output written to: " << filepath << "\n";
    return 0;
}
