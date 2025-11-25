#pragma once

#include <sys/types.h>
#include <cstddef>

/**
 * @brief Thin wrapper for System V message queues used for event passing.
 *
 * Use create() once, then send()/receive() to exchange typed messages by mtype.
 * All methods return success/failure so callers can log errno appropriately.
 */
class MessageQueue {
public:
    /** @brief Construct an empty handle (mqId = -1). */
    MessageQueue();
    ~MessageQueue();

    /**
     * @brief Create or get a queue for the given key.
     * @param key System V key (ftok or IPC_PRIVATE).
     * @param permissions file-mode-style permissions (default 0600).
     * @return true on success, false on failure.
     */
    bool create(key_t key, int permissions = 0600);

    /**
     * @brief Send a message of a given type.
     * @param msg pointer to message buffer (first field must be long mtype).
     * @param size number of bytes to send.
     * @param type message type value to set/read as mtype.
     * @return true on success, false on failure.
     */
    bool send(const void* msg, size_t size, long type);

    /**
     * @brief Receive a message of a given type.
     * @param buffer destination buffer.
     * @param size buffer size in bytes.
     * @param type desired mtype (0 to accept any).
     * @param flags msgrcv flags (e.g., IPC_NOWAIT).
     * @return true on success, false on failure.
     */
    bool receive(void* buffer, size_t size, long type, int flags = 0);

    /** @brief Underlying queue id, or -1 if not created. */
    int id() const;

private:
    int mqId;
};
