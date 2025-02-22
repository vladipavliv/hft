/**
 * @author Vladimir Pavliv
 * @date 2025-02-19
 */

#ifndef HFT_SERVER_TICKERDATA_HPP
#define HFT_SERVER_TICKERDATA_HPP

#include <atomic>

#include "boost_types.hpp"
#include "constants.hpp"
#include "market_types.hpp"
#include "order_book/flat_order_book.hpp"
#include "order_book/map_order_book.hpp"
#include "server_types.hpp"
#include "template_types.hpp"

namespace hft::server {

/**
 * @brief Need alignment cause when rebalancing would happen one thread could
 * increment the counter while another thread changes the threadId,
 * also price changes from separate thread
 * @todo Review later for improvement/reuse of empty space and data access patterns
 */
struct alignas(CACHE_LINE_SIZE) TickerData {
  TickerData() : orderBook{std::make_unique<OrderBook>()}, rerouteQueue{LFQueue<Order>(LFQ_SIZE)} {}

  inline void setThreadId(ThreadId id) { threadId.store(id, std::memory_order_release); }
  inline ThreadId getThreadId() const { return threadId.load(std::memory_order_acquire); }

  OrderBook::UPtr orderBook;
  std::atomic<ThreadId> threadId;
  LFQueue<Order> rerouteQueue;

  alignas(CACHE_LINE_SIZE) std::atomic<size_t> eventCounter;
  alignas(CACHE_LINE_SIZE) mutable std::atomic<Price> currentPrice;

  using UPtr = std::unique_ptr<TickerData>;
};

using AggregatorData = std::unordered_map<Ticker, TickerData, TickerHash>;

} // namespace hft::server

#endif // HFT_SERVER_TICKERDATA_HPP