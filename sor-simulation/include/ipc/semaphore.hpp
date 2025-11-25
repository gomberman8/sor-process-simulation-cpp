#pragma once

#include <sys/types.h>

/**
 * @brief System V semaphore wrapper for counting/binary semaphores.
 *
 * Use create() once, then wait()/post() around critical sections; destroy() at shutdown.
 */
class Semaphore {
public:
    /** @brief Construct an empty handle (semId = -1). */
    Semaphore();
    ~Semaphore();

    /**
     * @brief Create a semaphore set with one semaphore and initialize it.
     * @param key System V key (ftok or IPC_PRIVATE).
     * @param initialValue starting count value.
     * @param permissions file-mode-style permissions (default 0600).
     * @return true on success, false on failure.
     */
    bool create(key_t key, int initialValue, int permissions = 0600);

    /**
     * @brief P operation (decrement or block until available).
     * @return true on success, false on failure.
     */
    bool wait();

    /**
     * @brief V operation (increment/unlock).
     * @return true on success, false on failure.
     */
    bool post();

    /**
     * @brief Remove the semaphore set (IPC_RMID).
     * @return true on success, false on failure.
     */
    bool destroy();

    /** @brief Underlying semaphore id, or -1 if not created. */
    int id() const;

private:
    int semId;
};
