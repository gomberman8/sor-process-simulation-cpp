#include "ipc/shared_memory.hpp"

#include "util/error.hpp"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>
#include <cstring>

SharedMemory::SharedMemory() : shmId(-1), shmSize(0) {}

SharedMemory::~SharedMemory() = default;

// Allocate a shared memory segment and remember its size/id.
bool SharedMemory::create(key_t key, size_t size, int permissions) {
    shmSize = size;
    shmId = shmget(key, size, IPC_CREAT | permissions);
    if (shmId == -1) {
        logErrno("shmget failed");
        return false;
    }
    return true;
}

// Attach the shared segment into this process.
void* SharedMemory::attach() {
    if (shmId == -1) {
        logErrno("SharedMemory::attach called before create");
        return nullptr;
    }
    void* addr = shmat(shmId, nullptr, 0);
    if (addr == reinterpret_cast<void*>(-1)) {
        logErrno("shmat failed");
        return nullptr;
    }
    return addr;
}

// Detach a previously attached address.
bool SharedMemory::detach(const void* addr) {
    if (addr == nullptr) {
        logErrno("SharedMemory::detach null address");
        return false;
    }
    if (shmdt(addr) == -1) {
        logErrno("shmdt failed");
        return false;
    }
    return true;
}

// Remove the shared memory segment (IPC_RMID).
bool SharedMemory::destroy() {
    if (shmId == -1) {
        logErrno("SharedMemory::destroy called before create");
        return false;
    }
    if (shmctl(shmId, IPC_RMID, nullptr) == -1) {
        logErrno("shmctl IPC_RMID failed");
        return false;
    }
    return true;
}

// Open an existing segment by key without creating a new one.
bool SharedMemory::open(key_t key) {
    shmId = shmget(key, 0, 0);
    if (shmId == -1) {
        logErrno("shmget open failed");
        return false;
    }
    return true;
}

int SharedMemory::id() const {
    return shmId;
}
