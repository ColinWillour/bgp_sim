BGP SIMULATOR - CSE3150
Colin Willour
UCONN

A high performance C++ BGP simulator
 - Reads CAIDA AS-relationship data
 - Propagates announcements through the full internet topology
 - Outputs a RIB CSV for every AS in the graph

LIVE WEBSITE
 - https://bgpsim-colin.com
Cloudflare Pages: https://bgp-sim-9rx.pages.dev

BUILDING
c++ src/* -o bgp_simulator -std=c++23 -I include

RUNNING
  cd bin
  ./bgp_simulator \
      --relationships ../bench/many/CAIDAASGraphCollector_2025.10.16.txt \
      --announcements ../bench/many/anns.csv \
      --rov-asns      ../bench/many/rov_asns.csv

# Output is written to ribs.csv in the current directory

EXIT CODES
 - 0 = success
 - 1 = cycle detected in AS relationship

RUNNING TESTS
c++ -std=c++23 -I include -o tests/graph_tests \
    tests/graph_tests.cpp src/Route.cpp src/BGP.cpp \
    src/Router.cpp src/Network.cpp
./tests/graph_tests
# 98/98 pass

OPTIMIZATIONS

1) Linked-List Paths Instead of Vectors
      - The most important design decision in the entire program. Every
        time an announcement travels from one AS to the next, it needs to
        carry its full AS-path. The easiest way to do this is by making a
        vector and copying it at every hop. However with 78,000 routers and
        40 announcements producing around 9.8 million route objects, that
        means 9.8 million vector copies each allocating heap memory. This
        would produce a horrible runtime.
      - The solution is having each route store only its own ASN and
        a raw pointer to the previous route in a chain. The full path is never
        materialized until output time. Each hop costs a single pointer
        assignment which reduces memory allocation to almost nothing.

2) Pre-Reserved Hash Map
      - All 78,000 routers are stored in an unordered_map. The problem with
        hash maps is that when they get full they rehash — they allocate a new
        internal array and move everything into it. If you stored raw pointers
        into the map before a rehash, those pointers are now dangling and your
        program has undefined behavior.
      - The simple fix is to call reserve(90000) before inserting anything.
        This tells the map to allocate enough buckets upfront so it never
        needs to rehash during the build phase. Every raw Router* pointer
        stored in neighbor lists remains valid for the entire simulation
        lifetime. The alternative would have been to use indices into a vector
        instead of pointers, which is safer but more verbose and slightly slower.

3) Deferred Clique Pointer Resolution
      - The CAIDA file format lists the tier-1 clique members on a comment
        line near the top of the file, before most of those ASNs have actually
        been inserted into the router map. If we had resolved those ASNs to
        Router* pointers immediately during parsing, then later insertions
        would trigger a rehash and invalidate those pointers.
      - The fix is to store clique ASNs as plain uint32_t integers during
        parsing, then do a single pass to convert them to pointers after the
        entire file has been read and the map is fully built. This is a subtle
        bug that would only show up on real CAIDA data with tens of thousands
        of nodes — small test cases would never expose it.

4) Kahn's Algorithm for Topological Sort
      - To propagate announcements UP then ACROSS then DOWN, we need to know
        the rank of every AS — essentially how many hops from the bottom of
        the provider/customer hierarchy it sits. The naive approach would be a
        recursive DFS, but with 78,000 nodes and deep recursion stacks you
        risk a stack overflow.
      - Kahn's algorithm does this iteratively with a queue. Every AS starts
        with a remaining_ counter equal to its number of customers. Leaf nodes
        (no customers) start at zero and go into the queue at rank 0. When a
        node is processed, it decrements the counter of all its providers —
        when a provider hits zero it joins the next rank. This naturally produces
        the topological ordering we need. Cycle detection is also free: if the
        total nodes processed at the end is less than the total number of routers,
        at least one node was never reachable, meaning there is a cycle. The
        program exits with code 1.

5) Three-Phase Propagation with Peer Snapshot
      - BGP propagation follows a strict order: announcements go UP the hierarchy
        first, then ACROSS one peer hop, then DOWN. The up and down phases are
        straightforward — process ranks in order, send to providers or customers
        respectively.
      - The peer phase is trickier. If AS 1 sends to peer AS 2, and AS 2 immediately
        forwards to peer AS 3 in the same pass, the announcement has now traveled two
        peer hops — which violates valley-free routing. The fix is a snapshot: before
        any peer deliveries happen, every AS takes a snapshot of its current RIB. All
        peer deliveries are then made from those snapshots, not from live data. This
        guarantees each announcement travels exactly one peer hop per simulation run
        regardless of processing order.

6) ROV as a Policy Subclass
      - Route Origin Validation (ROV) is a defense where an AS drops announcements
        flagged as invalid. The clean object-oriented way to handle this is to make
        ROV a subclass of BGP that overrides the receive() method to check the
        rov_invalid flag before calling the parent implementation. If the flag is set,
        the route is silently dropped. If not, it falls through to normal BGP processing.
      - The alternative would have been an if statement inside the main BGP receive logic
        checking whether the current router has ROV enabled — but that pollutes the base
        class with policy-specific logic. The subclass approach keeps concerns separated
        and makes it trivial to add future policy types without touching existing code.
