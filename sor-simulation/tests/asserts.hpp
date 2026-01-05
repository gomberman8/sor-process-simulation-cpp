#pragma once

#include <iostream>

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        auto _lhs = (a); \
        auto _rhs = (b); \
        if (!(_lhs == _rhs)) { \
            std::cerr << "[FAIL] " << (msg) << " expected=" << _rhs << " actual=" << _lhs \
                      << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)
