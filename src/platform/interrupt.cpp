#include "platform/interrupt.hpp"

#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace netft_cli {
namespace {

std::atomic<InterruptFlag *> active_flag{nullptr};

#ifdef _WIN32

BOOL WINAPI request_interrupt(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) {
    return FALSE;
  }
  if (auto *flag = active_flag.load(std::memory_order_acquire); flag != nullptr) {
    flag->request();
  }
  return TRUE;
}

#else

extern "C" void request_interrupt(int /*signal_number*/) {
  if (auto *flag = active_flag.load(std::memory_order_acquire); flag != nullptr) {
    flag->request();
  }
}

#endif

} // namespace

class InterruptHandler::Implementation {
public:
  explicit Implementation(InterruptFlag &flag) {
    InterruptFlag *expected = nullptr;
    if (!active_flag.compare_exchange_strong(expected, &flag, std::memory_order_acq_rel)) {
      throw std::runtime_error("an interrupt handler is already active");
    }

#ifdef _WIN32
    if (::SetConsoleCtrlHandler(request_interrupt, TRUE) == FALSE) {
      active_flag.store(nullptr, std::memory_order_release);
      throw std::runtime_error("failed to install console interrupt handler");
    }
    installed_ = true;
#else
    struct sigaction action{};
    action.sa_handler = request_interrupt;
    if (::sigemptyset(&action.sa_mask) != 0 ||
        ::sigaction(SIGINT, &action, &previous_action_) != 0) {
      active_flag.store(nullptr, std::memory_order_release);
      throw std::runtime_error("failed to install signal interrupt handler");
    }
    installed_ = true;
#endif
  }

  ~Implementation() {
    if (!installed_) {
      return;
    }
#ifdef _WIN32
    static_cast<void>(::SetConsoleCtrlHandler(request_interrupt, FALSE));
#else
    static_cast<void>(::sigaction(SIGINT, &previous_action_, nullptr));
#endif
    active_flag.store(nullptr, std::memory_order_release);
  }

private:
  bool installed_{false};
#ifndef _WIN32
  struct sigaction previous_action_{};
#endif
};

InterruptHandler::InterruptHandler(InterruptFlag &flag)
    : implementation_(std::make_unique<Implementation>(flag)) {}

InterruptHandler::~InterruptHandler() = default;

} // namespace netft_cli
