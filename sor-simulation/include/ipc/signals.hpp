#pragma once

#include <signal.h>
#include <functional>

/**
 * @brief Helpers for setting simple C++ signal handlers.
 */
namespace Signals {
    using Handler = std::function<void(int)>;

    /**
     * @brief Install a handler for a given signal.
     * @param signum signal number (e.g., SIGUSR1).
     * @param handler function/lambda taking the signal number.
     * @return true on success, false on failure.
     */
    bool setHandler(int signum, Handler handler);

    /**
     * @brief Ignore a given signal (SIG_IGN).
     * @param signum signal number to ignore.
     */
    void ignore(int signum);
}
