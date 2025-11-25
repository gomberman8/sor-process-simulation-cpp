#pragma once

#include <random>

/**
 * @brief Wrapper around std::mt19937 for reproducible random values.
 */
class RandomGenerator {
public:
    /** @brief Seed with std::random_device for non-deterministic runs. */
    RandomGenerator();

    /** @brief Seed with a fixed value for deterministic runs. */
    explicit RandomGenerator(unsigned int seed);

    /**
     * @brief Inclusive integer range [min, max].
     */
    int uniformInt(int min, int max);

    /**
     * @brief Real range [min, max) using uniform_real_distribution.
     */
    double uniformReal(double min, double max);

private:
    std::mt19937 engine;
};
