#include "Route.h"
#include <vector>

std::string Route::format_path() const {
    std::vector<uint32_t> path;
    const Route* cur = this;
    while (cur != nullptr) {
        path.push_back(cur->holder);
        cur = cur->prev;
    }

    std::string result = "(";
    for (size_t i = 0; i < path.size(); i++) {
        result += std::to_string(path[i]);
        if (i + 1 < path.size()) result += ", ";
    }
    if (path.size() == 1) result += ",";
    result += ")";
    return result;
}
