#include "ipc/message_queue.hpp"

#include "util/error.hpp"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <cerrno>
#include <cstring>

MessageQueue::MessageQueue() : mqId(-1) {}

MessageQueue::~MessageQueue() = default;

bool MessageQueue::create(key_t key, int permissions) {
    mqId = msgget(key, IPC_CREAT | permissions);
    if (mqId == -1) {
        logErrno("msgget failed");
        return false;
    }
    return true;
}

bool MessageQueue::send(const void* msg, size_t size, long type) {
    if (mqId == -1) {
        logErrno("MessageQueue::send called before create");
        return false;
    }
    if (size < sizeof(long)) {
        logErrno("MessageQueue::send invalid size");
        return false;
    }

    // ensure mtype is set
    long* mutableMsg = const_cast<long*>(reinterpret_cast<const long*>(msg));
    mutableMsg[0] = type;
    size_t payloadSize = size - sizeof(long);

    if (msgsnd(mqId, msg, payloadSize, 0) == -1) {
        logErrno("msgsnd failed");
        return false;
    }
    return true;
}

bool MessageQueue::receive(void* buffer, size_t size, long type, int flags) {
    if (mqId == -1) {
        logErrno("MessageQueue::receive called before create");
        return false;
    }
    if (size < sizeof(long)) {
        logErrno("MessageQueue::receive invalid size");
        return false;
    }

    size_t payloadSize = size - sizeof(long);
    if (msgrcv(mqId, buffer, payloadSize, type, flags) == -1) {
        logErrno("msgrcv failed");
        return false;
    }
    return true;
}

int MessageQueue::id() const {
    return mqId;
}

bool MessageQueue::destroy() {
    if (mqId == -1) {
        logErrno("MessageQueue::destroy called before create");
        return false;
    }
    if (msgctl(mqId, IPC_RMID, nullptr) == -1) {
        logErrno("msgctl IPC_RMID failed");
        return false;
    }
    return true;
}
