#pragma once

#include <expected>
#include <utility>

// Propagates the error of a Result-returning expression from the enclosing
// function, which must itself return a Result.
#define SC_TRY(expr)                                                                                         \
    do {                                                                                                     \
        if (auto sc_try_result_ = (expr); !sc_try_result_)                                                   \
            return std::unexpected(std::move(sc_try_result_.error()));                                      \
    } while (0)
