#include "ipc/shared_memory.hpp"

#include "util/error.hpp"

#include <sys/ipc.h>
#include <sys/shm.h>
#include <cerrno>
#include <cstring>

SharedMemory::SharedMemory() : shmId(-1), shmSize(0) {}

SharedMemory::~SharedMemory() = default;

bool SharedMemory::create(key_t key, size_t size, int permissions) {
    shmSize = size;
    shmId = shmget(key, size, IPC_CREAT | permissions);
    if (shmId == -1) {
        logErrno("shmget failed");
        return false;
    }
    return true;
}

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

int SharedMemory::id() const {
    return shmId;
}
