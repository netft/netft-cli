#include "stream/acquisition.hpp"

#include "app/error.hpp"

#include <exception>
#include <utility>

namespace netft_cli {

Acquisition Acquisition::open(SensorBackend &backend, const ConnectionOptions &connection) {
  try {
    return Acquisition{backend.open(connection)};
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream could not be opened"};
  }
}

Acquisition::Acquisition(std::unique_ptr<SensorSession> session) : session_(std::move(session)) {
  if (!session_) {
    throw AppError{ExitCode::Stream, "sensor stream could not be opened"};
  }
}

Acquisition::~Acquisition() { stop(); }

void Acquisition::start(SensorSession::Callback callback) {
  try {
    session_->start(std::move(callback));
    active_ = true;
  } catch (const std::exception &) {
    session_->stop();
    throw AppError{ExitCode::Stream, "sensor stream could not be started"};
  }
}

void Acquisition::stop() noexcept {
  if (active_) {
    session_->stop();
    active_ = false;
  }
}

netft::HealthSnapshot Acquisition::health() const {
  try {
    return session_->health();
  } catch (const std::exception &) {
    throw AppError{ExitCode::Stream, "sensor stream health query failed"};
  }
}

} // namespace netft_cli
