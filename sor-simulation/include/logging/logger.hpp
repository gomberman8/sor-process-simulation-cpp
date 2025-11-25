#pragma once

#include <string>

#include "model/types.hpp"

/**
 * @brief Dedicated logger writing text lines to a file descriptor.
 */
class Logger {
public:
    /** @brief Default constructor leaves fd closed. */
    Logger();

    /**
     * @brief Construct and open a log file immediately.
     * @param path file path to open/create.
     */
    explicit Logger(const std::string& path);

    /**
     * @brief Open or create the log file.
     * @param path file path.
     * @return true on success, false on failure.
     */
    bool openFile(const std::string& path);

    /**
     * @brief Write one log line (implementation will append newline).
     * @param line text to write.
     */
    void logLine(const std::string& line);

    /**
     * @brief Close the file descriptor if open.
     */
    void closeFile();

private:
    int fd;
};

/**
 * @brief Blocking logger loop: read LogMessage from queue and write to file.
 * @param queueId message queue id for LOG_QUEUE.
 * @param path log file path.
 * @return 0 on clean exit, non-zero on error.
 */
int runLogger(int queueId, const std::string& path);

/**
 * @brief Convenience helper to send a LogMessage through LOG_QUEUE.
 * @param queueId message queue id for LOG_QUEUE.
 * @param role sender role.
 * @param simTime simulated time minutes.
 * @param text text payload.
 * @return true on success, false on failure.
 */
bool logEvent(int queueId, Role role, int simTime, const std::string& text);
