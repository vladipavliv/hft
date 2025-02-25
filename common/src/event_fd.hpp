/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_COMMON_CONDITIONVARIABLE_HPP
#define HFT_COMMON_CONDITIONVARIABLE_HPP

#include <atomic>
#include <spdlog/spdlog.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "constants.hpp"

namespace hft {

/**
 * @brief Lightweight condition variable to wake up threads
 * @details uses busy waiting for %BUSY_WAIT_CYCLES% to improve performance
 * and then waits on eventfd
 */
class EventFd {
public:
  EventFd() : mEfd{eventfd(0, EFD_SEMAPHORE)} {
    if (mEfd == -1) {
      throw std::runtime_error("Failed to create eventfd");
    }
  }
  ~EventFd() {
    notify();
    close(mEfd);
  }

  void wait(Predicate condition) {
    return;
    for (size_t i = 0; i < BUSY_WAIT_CYCLES; ++i) {
      if (condition()) {
        return;
      }
      std::this_thread::yield();
    }
    wait();
  }

  void wait() {
    return;
    mWaiting.store(true, std::memory_order_relaxed);
    uint64_t val;
    if (read(mEfd, &val, sizeof(val)) == -1) {
      spdlog::error("Failed to read from eventfd");
    }
  }

  void notify(bool force = false) {
    return;
    if (!force && !mWaiting.load(std::memory_order_relaxed)) {
      return;
    }
    uint64_t val{1};
    if (write(mEfd, &val, sizeof(val)) == -1) {
      spdlog::error("Failed to write to eventfd");
    }
    mWaiting.store(false, std::memory_order_relaxed);
  }

private:
  const int mEfd;
  std::atomic_bool mWaiting;
};

} // namespace hft

#endif // HFT_COMMON_CONDITIONVARIABLE_HPP
