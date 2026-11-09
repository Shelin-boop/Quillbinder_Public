#include "compile/Progress.h"

#include <QElapsedTimer>

namespace qb {
namespace compile {
namespace progress {

namespace {
std::function<void(double)> g_handler;
QElapsedTimer g_since;
bool g_inHandler = false;
} // namespace

void setHandler(std::function<void(double)> handler)
{
    g_handler = std::move(handler);
    g_since.start();
}

void tick(int done, int total)
{
    if (!g_handler || g_inHandler || g_since.elapsed() < 100)
        return;
    g_since.restart();
    g_inHandler = true; // a handler that lets the event loop run must not come back in here
    g_handler(total > 0 ? double(done) / total : -1.0);
    g_inHandler = false;
}

} // namespace progress
} // namespace compile
} // namespace qb
