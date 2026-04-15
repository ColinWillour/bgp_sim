#pragma once

#include <string>
#include <cstdint>
#include "Types.h"

// Route represents a BGP announcement traveling through the network.
//
// KEY DESIGN — linked list paths, zero vector copies:
//   Each Route stores a pointer to the previous Route instead of copying
//   the full path. During propagation this means zero heap allocations
//   for path data. The full path is only reconstructed at output time.
//
// Memory ownership:
//   Origin routes are owned by BGP::owned_ (inside the node's policy).
//   Forwarded routes are owned by Network::route_pool_.
//   All prev-pointers are valid for the lifetime of the simulation.

struct Route {
    std::string  prefix;       // e.g. "8.8.8.0/24" or "2001:db8::/32"
    uint32_t     holder;       // ASN of the node currently holding this route
    uint32_t     path_length;  // number of hops so far (1 = origin)
    Relationship recv_from;    // relationship this arrived on
    bool         rov_invalid;  // true = this prefix is hijacked/fake
    const Route* prev;         // previous hop in chain (nullptr = origin)

    // Create an origin route seeded at a node
    Route(const std::string& pfx, uint32_t asn, bool rov_inv)
        : prefix(pfx)
        , holder(asn)
        , path_length(1)
        , recv_from(Relationship::ORIGIN)
        , rov_invalid(rov_inv)
        , prev(nullptr)
    {}

    // Create a forwarded route arriving at new_holder from 'from'
    Route(const Route& from, uint32_t new_holder, Relationship rel)
        : prefix(from.prefix)
        , holder(new_holder)
        , path_length(from.path_length + 1)
        , recv_from(rel)
        , rov_invalid(from.rov_invalid)
        , prev(&from)
    {}

    // Routes must not be copied or moved — prev-pointers must stay stable
    Route(const Route&)            = delete;
    Route& operator=(const Route&) = delete;
    Route(Route&&)                 = delete;
    Route& operator=(Route&&)      = delete;

    // The "neighbor ASN" used for tiebreaking.
    // This is the next_hop: the ASN that directly sent us this route.
    // In the linked list, that is this route's holder (the node that forwarded it).
    uint32_t neighbor_asn() const {
        return holder;
    }

    // Check if a given ASN already appears in this route's path.
    // Used for BGP loop prevention: if our own ASN is in the path, drop it.
    bool path_contains(uint32_t asn) const {
        const Route* cur = this;
        while (cur != nullptr) {
            if (cur->holder == asn) return true;
            cur = cur->prev;
        }
        return false;
    }

    // Returns true if this route is better than 'other' for the same prefix.
    // Gao-Rexford priority: best relationship → shortest path → lowest neighbor ASN
    bool better_than(const Route& other) const {
        if (recv_from != other.recv_from)
            return recv_from < other.recv_from;
        if (path_length != other.path_length)
            return path_length < other.path_length;
        return neighbor_asn() < other.neighbor_asn();
    }

    // Reconstruct and format the full AS path for CSV output.
    // Walks the linked list and produces e.g. "(200, 100, 15169)" or "(15169,)"
    std::string format_path() const;
};
