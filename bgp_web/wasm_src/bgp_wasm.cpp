#include <cstdlib>
#include "Network.h"
#include <emscripten/emscripten.h>

static Network* g_net = nullptr;

extern "C" {

EMSCRIPTEN_KEEPALIVE
int bgp_run(const char* rel_path, const char* ann_path,
            const char* rov_path, const char* out_path)
{
    delete g_net;
    g_net = new Network();

    int rc = 0;
    if ((rc = g_net->load_rov(rov_path)) != 0) return rc;
    if ((rc = g_net->build(rel_path))    != 0) return rc;
    if ((rc = g_net->seed(ann_path))     != 0) return rc;
    if ((rc = g_net->propagate())        != 0) return rc;
    if ((rc = g_net->output(out_path))   != 0) return rc;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void bgp_free()
{
    delete g_net;
    g_net = nullptr;
}

} // extern "C"
