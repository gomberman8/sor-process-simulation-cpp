#include "ipc/semaphore.hpp"

#include "util/error.hpp"

#include <sys/ipc.h>
#include <sys/sem.h>
#include <cerrno>
#include <cstring>

Semaphore::Semaphore() : semId(-1) {}

Semaphore::~Semaphore() = default;

bool Semaphore::create(key_t key, int initialValue, int permissions) {
    semId = semget(key, 1, IPC_CREAT | permissions);
    if (semId == -1) {
        logErrno("semget failed");
        return false;
    }
    if (semctl(semId, 0, SETVAL, initialValue) == -1) {
        logErrno("semctl SETVAL failed");
        return false;
    }
    return true;
}

bool Semaphore::wait() {
    if (semId == -1) {
        logErrno("Semaphore::wait called before create");
        return false;
    }
    struct sembuf op {0, -1, 0};
    if (semop(semId, &op, 1) == -1) {
        logErrno("semop wait failed");
        return false;
    }
    return true;
}

bool Semaphore::post() {
    if (semId == -1) {
        logErrno("Semaphore::post called before create");
        return false;
    }
    struct sembuf op {0, 1, 0};
    if (semop(semId, &op, 1) == -1) {
        logErrno("semop post failed");
        return false;
    }
    return true;
}

bool Semaphore::destroy() {
    if (semId == -1) {
        logErrno("Semaphore::destroy called before create");
        return false;
    }
    if (semctl(semId, 0, IPC_RMID) == -1) {
        logErrno("semctl IPC_RMID failed");
        return false;
    }
    return true;
}

bool Semaphore::open(key_t key) {
    semId = semget(key, 1, 0);
    if (semId == -1) {
        logErrno("semget open failed");
        return false;
    }
    return true;
}

int Semaphore::id() const {
    return semId;
}
