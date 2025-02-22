/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_BALANCING_EVENTSINK_HPP
#define HFT_BALANCING_EVENTSINK_HPP

#include <functional>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>

#include "boost_types.hpp"
#include "constants.hpp"
#include "event_fd.hpp"
#include "market_types.hpp"
#include "network_types.hpp"
#include "template_types.hpp"
#include "types.hpp"
#include "utils/string_utils.hpp"
#include "utils/utils.hpp"

namespace hft {

/**
 * @brief Distributes events by Ticker across N threads
 * @details Every thread has its own set of queues. Tickers are distributed among threads
 * and each thread works with order books synchronously. Rebalancing is suggested to be done
 * via data aggregator, that holds a map Ticker -> ThreadId, so it can be done atomicly
 * by redirecting to another ThreadId, letting previous thread process its incoming orders
 * meanwhile working on another queues, once OrderBook is free, new thread takes over
 */
template <typename EventConsumer, typename... EventTypes>
class PartitionEventSink {
public:
  using Consumer = EventConsumer;

  PartitionEventSink() : mEFds(Config().cfg.coresApp.size()) {
    const auto threads = Config().cfg.coresApp.size();
    mEventQueues.reserve(threads);
    std::generate_n(std::back_inserter(mEventQueues), threads,
                    [this]() { return createLFQueueTuple<EventTypes...>(LFQ_SIZE); });
  }

  ~PartitionEventSink() { stop(); }

  void setConsumer(Consumer *consumer) { mConsumer = consumer; }

  void start() {
    if (Config::cfg.coresApp.empty()) {
      spdlog::error("No cores provided");
      assert(false);
      return;
    }
    mThreads.reserve(Config::cfg.coresApp.size());
    for (size_t i = 0; i < Config().cfg.coresApp.size(); ++i) {
      mThreads.emplace_back([this, i] {
        try {
          mThreadId = i;
          ThreadId core = Config().cfg.coresApp[i];
          spdlog::debug("Started worker thread {} on the core: {}", i, core);
          utils::pinThreadToCore(core);
          utils::setTheadRealTime();
          processEvents();
          spdlog::debug("Finished worker thread {}", i);
        } catch (const std::exception &e) {
          spdlog::error(e.what());
        }
      });
    }
  }

  void stop() {
    spdlog::trace("Stoping PartitionEventSink");
    if (mStop.test()) {
      return;
    }
    mStop.test_and_set();
    for (auto &fd : mEFds) {
      fd.notify();
    }
    for (auto &thread : mThreads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  template <typename EventType>
  void post(const EventType &event) {
    auto id = mConsumer->getWorkerId(event);
    if (id == std::numeric_limits<ThreadId>::max()) {
      spdlog::error("Unknown event");
      return;
    }
    auto &queue = getQueue<EventType>(id);
    while (!queue.push(event)) {
      std::this_thread::yield();
    }
    mEFds[id].notify();
  }

  template <typename EventType>
  void post(Span<EventType> events) {
    for (auto &event : events) {
      post<EventType>(event);
    }
  }

private:
  void processEvents() {
    mEFds[mThreadId].wait();
    while (!mStop.test()) {
      (processQueue<EventTypes>(mThreadId), ...);
      mEFds[mThreadId].wait([this]() { return !isTupleEmpty(mEventQueues[mThreadId]); });
    }
  }

  template <typename EventType>
  void processQueue(ThreadId id) {
    auto &queue = getQueue<EventType>(id);
    if (queue.empty()) {
      return;
    }
    std::vector<EventType> buffer;
    buffer.reserve(LFQ_POP_LIMIT);
    EventType event;
    while (buffer.size() < LFQ_POP_LIMIT && queue.pop(event)) {
      buffer.emplace_back(event);
    }
    mConsumer->onEvent(mThreadId, Span<EventType>(buffer));
  }

  template <typename EventType>
  LFQueue<EventType> &getQueue(ThreadId id) {
    auto &tupl = mEventQueues[id];
    return *std::get<UPtrLFQueue<EventType>>(tupl);
  }

private:
  Consumer *mConsumer;
  // Every thread has its own tuple with queues for events
  std::vector<std::tuple<UPtrLFQueue<EventTypes>...>> mEventQueues;
  std::vector<std::thread> mThreads;
  std::vector<EventFd> mEFds;

  static thread_local ThreadId mThreadId;
  std::atomic_flag mStop = ATOMIC_FLAG_INIT;
};

template <typename EventConsumer, typename... EventTypes>
thread_local ThreadId PartitionEventSink<EventConsumer, EventTypes...>::mThreadId = 0;

} // namespace hft

#endif // HFT_BALANCING_EVENTSINK_HPP