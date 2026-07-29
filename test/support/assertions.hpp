#pragma once

#include "app/error.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace netft_cli::test {

template <typename Callable> void expect_app_error(ExitCode expected, Callable &&callable) {
  try {
    std::forward<Callable>(callable)();
    ADD_FAILURE() << "Expected AppError";
  } catch (const AppError &error) {
    EXPECT_EQ(error.code(), expected);
  } catch (...) {
    ADD_FAILURE() << "Expected AppError";
  }
}

} // namespace netft_cli::test
