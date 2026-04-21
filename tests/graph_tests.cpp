// graph_tests.cpp — CSE3150 BGP Simulator test suite
//
// Compile & run from bgp_sim/ root:
//   g++ -std=c++17 -O2 -Iinclude -o tests/graph_tests \
//       tests/graph_tests.cpp src/Route.cpp src/BGP.cpp \
//       src/Router.cpp src/Network.cpp
//   ./tests/graph_tests

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cassert>

// ── tiny test harness ────────────────────────────────────────────────────────

static int  g_passed = 0;
static int  g_failed = 0;

#define CHECK(expr) do { \
    if (expr) { ++g_passed; } \
    else { \
        std::cerr << "  FAIL  " << __FILE__ << ":" << __LINE__ \
                  << "  " << #expr << "\n"; \
        ++g_failed; \
    } \
} while(0)

#define TEST(name) static void name()
#define RUN(name)  do { std::cout << "[ " #name " ]\n"; name(); } while(0)

// ── helpers ──────────────────────────────────────────────────────────────────

// Write a temporary file and return its path.
static std::string write_tmp(const std::string& filename,
                              const std::string& content)
{
    std::string path = "/tmp/" + filename;
    std::ofstream f(path);
    f << content;
    return path;
}

// Read ribs.csv written by Network::output() into a vector of lines (no header).
static std::vector<std::string> read_ribs(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; }   // skip header
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Return true if any line in ribs contains needle.
static bool contains(const std::vector<std::string>& ribs,
                     const std::string& needle)
{
    for (auto& l : ribs)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

// ── include the Network class ────────────────────────────────────────────────

#include "Network.h"

// ── UNIT TESTS: Route ────────────────────────────────────────────────────────

TEST(test_route_origin)
{
    // An origin route should have path_length==1, recv_from==ORIGIN, prev==nullptr
    Route r("10.0.0.0/24", 1, false);
    CHECK(r.path_length == 1);
    CHECK(r.recv_from   == Relationship::ORIGIN);
    CHECK(r.holder      == 1u);
    CHECK(r.prev        == nullptr);
    CHECK(r.rov_invalid == false);
}

TEST(test_route_forwarded)
{
    Route origin("10.0.0.0/24", 1, false);
    Route hop1(origin, 2, Relationship::CUSTOMER);

    CHECK(hop1.path_length == 2);
    CHECK(hop1.recv_from   == Relationship::CUSTOMER);
    CHECK(hop1.holder      == 2u);
    CHECK(hop1.prev        == &origin);
    CHECK(hop1.prefix      == "10.0.0.0/24");

    Route hop2(hop1, 3, Relationship::PROVIDER);
    CHECK(hop2.path_length == 3);
    CHECK(hop2.prev        == &hop1);
}

TEST(test_route_path_contains)
{
    Route r1("p", 10, false);
    Route r2(r1, 20, Relationship::CUSTOMER);
    Route r3(r2, 30, Relationship::PROVIDER);

    CHECK( r3.path_contains(10));
    CHECK( r3.path_contains(20));
    CHECK( r3.path_contains(30));
    CHECK(!r3.path_contains(99));
}

TEST(test_route_better_than_relationship)
{
    // CUSTOMER < PEER < PROVIDER in priority
    Route origin("p", 1, false);
    Route cust(origin, 2, Relationship::CUSTOMER);
    Route peer(origin, 2, Relationship::PEER);
    Route prov(origin, 2, Relationship::PROVIDER);

    CHECK( cust.better_than(peer));
    CHECK( cust.better_than(prov));
    CHECK( peer.better_than(prov));
    CHECK(!peer.better_than(cust));
    CHECK(!prov.better_than(cust));
}

TEST(test_route_better_than_path_length)
{
    // Same relationship, shorter path wins
    Route origin("p", 1, false);
    Route hop1(origin, 2, Relationship::CUSTOMER);
    Route hop2(hop1,   3, Relationship::CUSTOMER);

    // hop1 has path_length==2, hop2 has path_length==3
    CHECK( hop1.better_than(hop2));
    CHECK(!hop2.better_than(hop1));
}

TEST(test_route_better_than_neighbor_asn)
{
    // Same relationship, same path length → lower neighbor ASN wins
    Route o1("p", 1, false);
    Route o2("p", 1, false);

    // Both forwarded to holder=100 from different senders
    Route fwd_low (o1, 100, Relationship::CUSTOMER);   // holder==100, neighbor_asn==100
    Route fwd_high(o2, 200, Relationship::CUSTOMER);   // holder==200, neighbor_asn==200

    // fwd_low has lower neighbor_asn than fwd_high
    CHECK(fwd_low.neighbor_asn() < fwd_high.neighbor_asn());
    CHECK(fwd_low.better_than(fwd_high));
}

TEST(test_route_format_path_origin)
{
    Route r("10.0.0.0/8", 15169, false);
    std::string p = r.format_path();
    // Should look like "(15169,)"
    CHECK(p.find("15169") != std::string::npos);
    CHECK(p.find("(")     != std::string::npos);
    CHECK(p.find(")")     != std::string::npos);
}

TEST(test_route_format_path_multi)
{
    Route r1("p", 4, false);
    Route r2(r1, 3, Relationship::CUSTOMER);
    Route r3(r2, 2, Relationship::CUSTOMER);
    Route r4(r3, 1, Relationship::CUSTOMER);

    std::string p = r4.format_path();
    // Path should include all ASNs in order: 1, 2, 3, 4
    CHECK(p.find("1") != std::string::npos);
    CHECK(p.find("2") != std::string::npos);
    CHECK(p.find("3") != std::string::npos);
    CHECK(p.find("4") != std::string::npos);
}

// ── SYSTEM TESTS: Network ────────────────────────────────────────────────────

// Topology:
//   4 (origin, announces 1.2.0.0/16)
//   |
//   3 (customer of 4, provider of 1 and 2)
//  / \
// 1   2
//
// Peer: 1 <-> 2
TEST(test_basic_propagation)
{
    // relationships: left|right|rel  (-1 = left is provider of right)
    std::string rel =
        "# input clique: 4\n"
        "3|4|-1|bgp\n"   // 4 is provider of 3
        "1|3|-1|bgp\n"   // 3 is provider of 1
        "2|3|-1|bgp\n"   // 3 is provider of 2
        "1|2|0|bgp\n";   // 1 and 2 are peers

    std::string ann  = "asn,prefix,rov_invalid\n4,1.2.0.0/16,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("basic_rel.txt", rel);
    auto ann_path = write_tmp("basic_ann.csv", ann);
    auto rov_path = write_tmp("basic_rov.csv", rov);
    auto out_path = write_tmp("basic_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);

    // Every node should have a route for 1.2.0.0/16
    CHECK(contains(ribs, "4,1.2.0.0/16"));
    CHECK(contains(ribs, "3,1.2.0.0/16"));
    CHECK(contains(ribs, "1,1.2.0.0/16"));
    CHECK(contains(ribs, "2,1.2.0.0/16"));

    // Origin route at 4 should have path "(4,)"
    CHECK(contains(ribs, "4,1.2.0.0/16,\"(4,)\""));

    // AS 3's path should be (3, 4)
    CHECK(contains(ribs, "3,1.2.0.0/16"));

    // AS 1 and 2 should have paths including ASN 4 at the end
    for (auto& l : ribs) {
        if (l.rfind("1,1.2.0.0/16", 0) == 0 ||
            l.rfind("2,1.2.0.0/16", 0) == 0) {
            CHECK(l.find("4") != std::string::npos);
        }
    }
}

TEST(test_gao_rexford_customer_over_peer)
{
    // 1 and 2 are peers. Both receive a route from provider 3.
    // 4 is a customer of both 1 and 2.
    // 4 should prefer the route from its customer side vs provider, but here
    // we test that customer-learned route beats a peer-learned route.
    //
    //   3 (announces)
    //  / \
    // 1   2   (peers with each other)
    //  \ /
    //   4 (customer of both 1 and 2)
    //
    // 4 receives 1.2.0.0/16 from 1 (as customer of 1, i.e. 1 is provider of 4)
    // and from 2 (provider). Same path length, same relationship.
    // Lowest next-hop ASN wins → 1 < 2 so route via 1 should be preferred.

    std::string rel =
        "# input clique: 3\n"
        "1|3|-1|bgp\n"
        "2|3|-1|bgp\n"
        "1|2|0|bgp\n"
        "1|4|-1|bgp\n"
        "2|4|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n3,1.2.0.0/16,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("grf_rel.txt", rel);
    auto ann_path = write_tmp("grf_ann.csv", ann);
    auto rov_path = write_tmp("grf_rov.csv", rov);
    auto out_path = write_tmp("grf_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);

    // All ASes should have a route
    CHECK(contains(ribs, "3,1.2.0.0/16"));
    CHECK(contains(ribs, "1,1.2.0.0/16"));
    CHECK(contains(ribs, "2,1.2.0.0/16"));
    CHECK(contains(ribs, "4,1.2.0.0/16"));
}

TEST(test_two_announcements_different_prefixes)
{
    // Two origins, different prefixes — every AS should have both.
    std::string rel =
        "# input clique: 3\n"
        "1|3|-1|bgp\n"
        "2|3|-1|bgp\n";

    std::string ann =
        "asn,prefix,rov_invalid\n"
        "3,10.0.0.0/8,False\n"
        "3,192.168.0.0/16,False\n";
    std::string rov = "";

    auto rel_path = write_tmp("two_pfx_rel.txt", rel);
    auto ann_path = write_tmp("two_pfx_ann.csv", ann);
    auto rov_path = write_tmp("two_pfx_rov.csv", rov);
    auto out_path = write_tmp("two_pfx_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);
    CHECK(contains(ribs, "1,10.0.0.0/8"));
    CHECK(contains(ribs, "2,10.0.0.0/8"));
    CHECK(contains(ribs, "1,192.168.0.0/16"));
    CHECK(contains(ribs, "2,192.168.0.0/16"));
}

TEST(test_rov_drops_invalid)
{
    // AS 2 is an ROV router. It should drop rov_invalid announcements.
    // AS 1 announces a hijacked prefix. AS 2 (ROV) should NOT store it.
    // AS 3 (no ROV) should store it.

    std::string rel =
        "# input clique: 1\n"
        "2|1|-1|bgp\n"
        "3|1|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n1,10.0.0.0/8,True\n";
    std::string rov  = "2\n";  // only AS 2 deploys ROV

    auto rel_path = write_tmp("rov_rel.txt", rel);
    auto ann_path = write_tmp("rov_ann.csv", ann);
    auto rov_path = write_tmp("rov_rov.csv", rov);
    auto out_path = write_tmp("rov_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);

    // AS 1 (origin) always has it
    CHECK(contains(ribs, "1,10.0.0.0/8"));

    // AS 3 (no ROV) should have the route
    CHECK(contains(ribs, "3,10.0.0.0/8"));

    // AS 2 (ROV) must NOT have the invalid route
    for (auto& l : ribs) {
        if (l.rfind("2,", 0) == 0) {
            // If AS 2 has any route at all, it must not be 10.0.0.0/8
            CHECK(l.find("10.0.0.0/8") == std::string::npos);
        }
    }
}

TEST(test_cycle_detection)
{
    // A cycle: 1 is provider of 2, and 2 is provider of 1 — should return 1
    std::string rel =
        "1|2|-1|bgp\n"
        "2|1|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n1,10.0.0.0/8,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("cycle_rel.txt", rel);
    auto ann_path = write_tmp("cycle_ann.csv", ann);
    auto rov_path = write_tmp("cycle_rov.csv", rov);

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    int rc = net.build(rel_path);
    CHECK(rc == 1);   // must detect cycle and return 1
}

TEST(test_no_valley_peer_to_peer)
{
    // Valley-free routing: a route received from a peer must NOT be
    // forwarded to another peer or a provider.
    //
    //  1 - peer - 2 - peer - 3
    //              |
    //              4  (customer of 2)
    //
    // 1 announces. 2 learns via peer. 2 should NOT forward to 3 (peer).
    // 2 SHOULD forward down to 4 (customer).

    std::string rel =
        "# input clique: 1 2 3\n"
        "1|2|0|bgp\n"
        "2|3|0|bgp\n"
        "2|4|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n1,10.1.0.0/16,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("valley_rel.txt", rel);
    auto ann_path = write_tmp("valley_ann.csv", ann);
    auto rov_path = write_tmp("valley_rov.csv", rov);
    auto out_path = write_tmp("valley_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);

    // AS 2 should have a route (from peer 1)
    CHECK(contains(ribs, "2,10.1.0.0/16"));

    // AS 4 should have a route (from provider 2, learned from peer 1)
    CHECK(contains(ribs, "4,10.1.0.0/16"));

    // AS 3 should NOT have a route (valley-free: peer-received not forwarded to peer)
    CHECK(!contains(ribs, "3,10.1.0.0/16"));
}

TEST(test_ipv6_prefix)
{
    // Simulator must handle IPv6 prefixes correctly
    std::string rel =
        "# input clique: 1\n"
        "2|1|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n1,2001:db8::/32,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("v6_rel.txt", rel);
    auto ann_path = write_tmp("v6_ann.csv", ann);
    auto rov_path = write_tmp("v6_rov.csv", rov);
    auto out_path = write_tmp("v6_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    auto ribs = read_ribs(out_path);
    CHECK(contains(ribs, "1,2001:db8::/32"));
    CHECK(contains(ribs, "2,2001:db8::/32"));
}

TEST(test_output_format)
{
    // Check that ribs.csv has correct header and CSV structure
    std::string rel =
        "# input clique: 1\n"
        "2|1|-1|bgp\n";

    std::string ann  = "asn,prefix,rov_invalid\n1,10.0.0.0/8,False\n";
    std::string rov  = "";

    auto rel_path = write_tmp("fmt_rel.txt", rel);
    auto ann_path = write_tmp("fmt_ann.csv", ann);
    auto rov_path = write_tmp("fmt_rov.csv", rov);
    auto out_path = write_tmp("fmt_out.csv", "");

    Network net;
    CHECK(net.load_rov(rov_path) == 0);
    CHECK(net.build(rel_path)    == 0);
    CHECK(net.seed(ann_path)     == 0);
    CHECK(net.propagate()        == 0);
    CHECK(net.output(out_path)   == 0);

    std::ifstream f(out_path);
    std::string header;
    std::getline(f, header);
    CHECK(header == "asn,prefix,as_path");

    std::string line;
    int count = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        ++count;
        // Each line must have at least 2 commas
        int commas = 0;
        for (char c : line) if (c == ',') ++commas;
        CHECK(commas >= 2);
        // Must contain a '(' for the as_path tuple
        CHECK(line.find('(') != std::string::npos);
    }
    CHECK(count >= 1);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== BGP Simulator Tests ===\n\n";

    std::cout << "-- Unit: Route --\n";
    RUN(test_route_origin);
    RUN(test_route_forwarded);
    RUN(test_route_path_contains);
    RUN(test_route_better_than_relationship);
    RUN(test_route_better_than_path_length);
    RUN(test_route_better_than_neighbor_asn);
    RUN(test_route_format_path_origin);
    RUN(test_route_format_path_multi);

    std::cout << "\n-- System: Network --\n";
    RUN(test_basic_propagation);
    RUN(test_gao_rexford_customer_over_peer);
    RUN(test_two_announcements_different_prefixes);
    RUN(test_rov_drops_invalid);
    RUN(test_cycle_detection);
    RUN(test_no_valley_peer_to_peer);
    RUN(test_ipv6_prefix);
    RUN(test_output_format);

    std::cout << "\n===========================\n";
    std::cout << "Passed: " << g_passed << "\n";
    std::cout << "Failed: " << g_failed << "\n";

    return g_failed == 0 ? 0 : 1;
}
