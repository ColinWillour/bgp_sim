#include <iostream>
#include <string>
#include <cstring>
#include "Network.h"

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --relationships <file>"
              << " --announcements <file>"
              << " --rov-asns <file>\n";
}

int main(int argc, char* argv[]) {
    std::string rel_file, ann_file, rov_file;

    for (int i = 1; i < argc; i++) {
        if      (!std::strcmp(argv[i], "--relationships") && i+1 < argc) rel_file = argv[++i];
        else if (!std::strcmp(argv[i], "--announcements") && i+1 < argc) ann_file = argv[++i];
        else if (!std::strcmp(argv[i], "--rov-asns")      && i+1 < argc) rov_file = argv[++i];
        else { std::cerr << "Unknown argument: " << argv[i] << "\n"; print_usage(argv[0]); return 1; }
    }

    if (rel_file.empty() || ann_file.empty() || rov_file.empty()) {
        std::cerr << "Error: Missing required arguments.\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "=== BGP Simulator ===\n"
              << "Relationships : " << rel_file << "\n"
              << "Announcements : " << ann_file << "\n"
              << "ROV ASNs      : " << rov_file << "\n\n";

    Network net;
    int s = 0;

    if ((s = net.load_rov(rov_file))  != 0) return s;
    if ((s = net.build(rel_file))     != 0) return s;
    if ((s = net.seed(ann_file))      != 0) return s;
    if ((s = net.propagate())         != 0) return s;
    if ((s = net.output("ribs.csv"))  != 0) return s;

    std::cout << "\nDone.\n";
    return 0;
}
