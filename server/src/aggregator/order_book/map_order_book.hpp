/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_SERVER_MAPORDERBOOK_HPP
#define HFT_SERVER_MAPORDERBOOK_HPP

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "constants.hpp"
#include "market_types.hpp"
#include "template_types.hpp"
#include "types.hpp"
#include "utils/string_utils.hpp"

namespace hft::server {

/**
 * @brief OrderBook base on std::map. Supports basic acquire/release mechanism
 * for atomic modifications
 */
class MapOrderBook {
public:
  using UPtr = std::unique_ptr<MapOrderBook>;

  MapOrderBook() = default;
  ~MapOrderBook() = default;

  inline bool acquire() { return !mBusy.test_and_set(std::memory_order_acq_rel); }
  inline void release() { mBusy.clear(std::memory_order_acq_rel); }

  void add(Span<Order> orders) {
    for (auto &order : orders) {
      if (order.action == OrderAction::Buy) {
        if (mBids.size() > ORDER_BOOK_LIMIT) {
          spdlog::error("Limit reached {}", [&order] { return utils::toStrView(order.ticker); }());
          continue;
        }
        auto &priceBids = mBids[order.price];
        if (priceBids.empty()) {
          priceBids.reserve(ORDER_BOOK_LIMIT);
        }
        priceBids.emplace_back(order);
        mLastAdded.insert(order.id); // Randomizator
      } else {
        if (mAsks.size() > ORDER_BOOK_LIMIT) {
          spdlog::error("Limit reached {}", [&order] { return utils::toStrView(order.ticker); }());
          return;
        }
        auto &priceBids = mAsks[order.price];
        if (priceBids.empty()) {
          priceBids.reserve(ORDER_BOOK_LIMIT);
        }
        priceBids.emplace_back(order);
        mLastAdded.insert(order.id); // Randomizator
      }
    }
  }

  std::vector<OrderStatus> match() {
    std::vector<OrderStatus> matches;
    matches.reserve(10);
    while (!mBids.empty() && !mAsks.empty()) {
      auto &bestBids = mBids.begin()->second;
      auto &bestAsks = mAsks.begin()->second;

      if (bestBids.back().price < bestAsks.back().price) {
        break;
      }
      auto &bestBid = bestBids.back();
      auto &bestAsk = bestAsks.back();

      int quantity = std::min(bestBid.quantity, bestAsk.quantity);

      bestBid.quantity -= quantity;
      bestAsk.quantity -= quantity;

      if (mLastAdded.contains(bestBid.id)) {
        matches.emplace_back(orderMatch(bestBid, quantity, bestAsk.price));
      }
      if (mLastAdded.contains(bestAsk.id)) {
        matches.emplace_back(orderMatch(bestAsk, quantity, bestAsk.price));
      }

      if (bestBid.quantity == 0) {
        bestBids.pop_back();
        if (bestBids.empty()) {
          mBids.erase(mBids.begin());
        }
      }
      if (bestAsk.quantity == 0) {
        bestAsks.pop_back();
        if (bestAsks.empty()) {
          mAsks.erase(mAsks.begin());
        }
      }
    }
    mLastAdded.clear();
    return matches;
  }

private:
  OrderStatus orderMatch(const Order &order, Quantity quantity, Price price) {
    OrderStatus status;
    status.id = order.id;
    status.quantity = quantity;
    status.fillPrice = price;
    status.state = order.quantity == 0 ? OrderState::Full : OrderState::Partial;
    status.action = order.action;
    status.traderId = order.traderId;
    status.ticker = order.ticker;
    spdlog::info([&status] { return utils::toString(status); }());
    return status;
  }

private:
  std::map<uint32_t, std::vector<Order>, std::greater<uint32_t>> mBids;
  std::map<uint32_t, std::vector<Order>> mAsks;
  /* Randomizator: orders that have been added get placed in the set until match() is called
  then upon matching only the last added orders get notification sent to the client
  this way we simulate lots of traders for the sake of testing */
  std::set<OrderId> mLastAdded;
  std::atomic_flag mBusy = ATOMIC_FLAG_INIT;
};

} // namespace hft::server

#endif // HFT_SERVER_MAPORDERBOOK_HPP
