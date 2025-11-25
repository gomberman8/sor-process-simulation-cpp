#pragma once

#include <sys/types.h>
#include <cstddef>

/**
 * @brief System V shared memory helper for the SharedState segment.
 *
 * Typical usage: create() -> attach() -> use memory -> detach() -> destroy().
 */
class SharedMemory {
public:
    /** @brief Construct an empty handle (shmId = -1). */
    SharedMemory();
    ~SharedMemory();

    /**
     * @brief Create or get a shared memory segment.
     * @param key System V key (ftok or IPC_PRIVATE).
     * @param size required size in bytes.
     * @param permissions file-mode-style permissions (default 0600).
     * @return true on success, false otherwise.
     */
    bool create(key_t key, size_t size, int permissions = 0600);

    /**
     * @brief Attach the segment to the process address space.
     * @return pointer to mapped area on success; nullptr on failure.
     */
    void* attach();

    /**
     * @brief Detach a previously attached address.
     * @param addr pointer returned by attach().
     * @return true on success, false on failure.
     */
    bool detach(const void* addr);

    /**
     * @brief Mark the segment for destruction (IPC_RMID).
     * @return true on success, false on failure.
     */
    bool destroy();

    /**
     * @brief Open an existing segment by key without creating a new one.
     * @param key System V key.
     * @return true on success, false on failure.
     */
    bool open(key_t key);

    /** @brief Underlying shm id, or -1 if not created. */
    int id() const;

private:
    int shmId;
    size_t shmSize;
};
