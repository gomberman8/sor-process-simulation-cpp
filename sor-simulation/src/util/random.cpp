#include "util/random.hpp"

RandomGenerator::RandomGenerator() : engine(std::random_device{}()) {}

RandomGenerator::RandomGenerator(unsigned int seed) : engine(seed) {}

int RandomGenerator::uniformInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(engine);
}

double RandomGenerator::uniformReal(double min, double max) {
    std::uniform_real_distribution<double> dist(min, max);
    return dist(engine);
}
