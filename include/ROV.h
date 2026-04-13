#pragma once

#include "BGP.h"

class ROV : public BGP {
public:
    void receive(const Route* route, uint32_t my_asn) override {
        if (route->rov_invalid) return;
        BGP::receive(route, my_asn);
    }
};
