#include "ipc/signals.hpp"

#include "util/error.hpp"

#include <map>

namespace {
std::map<int, Signals::Handler>& handlerMap() {
    static std::map<int, Signals::Handler> handlers;
    return handlers;
}

void dispatch(int signum) {
    auto it = handlerMap().find(signum);
    if (it != handlerMap().end() && it->second) {
        it->second(signum);
    }
}
} // namespace

namespace Signals {

bool setHandler(int signum, Handler handler) {
    // Install a std::function-backed handler via sigaction.
    struct sigaction sa {};
    sa.sa_handler = dispatch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    handlerMap()[signum] = std::move(handler);
    if (sigaction(signum, &sa, nullptr) == -1) {
        logErrno("sigaction failed");
        return false;
    }
    return true;
}

void ignore(int signum) {
    // Install SIG_IGN for the given signal.
    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signum, &sa, nullptr);
}

} // namespace Signals
