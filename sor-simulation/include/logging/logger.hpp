#pragma once

#include <string>

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
