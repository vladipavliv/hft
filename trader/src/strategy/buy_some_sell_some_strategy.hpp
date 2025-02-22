/**
 * @author Vladimir Pavliv
 * @date 2025-02-14
 */

#ifndef HFT_TRADER_BUYSOMESELLSOMESTRATEGY_HPP
#define HFT_TRADER_BUYSOMESELLSOMESTRATEGY_HPP

#include <atomic>
#include <deque>

#include "boost_types.hpp"
#include "db/postgres_adapter.hpp"
#include "market_types.hpp"
#include "rtt_tracker.hpp"
#include "trader_types.hpp"
#include "trading_stats.hpp"
#include "types.hpp"
#include "utils/rng.hpp"
#include "utils/string_utils.hpp"

namespace hft::trader {

/**
 * @brief AKA BSSS Strategy
 */
class BuySomeSellSomeStrategy {
public:
  BuySomeSellSomeStrategy(TraderSink &sink)
      : mSink{sink}, mPrices{db::PostgresAdapter::readTickers()},
        mTradeRate{Config::cfg.tradeRateUs}, mTimer{mSink.ctx()} {
    mStats.balance = utils::RNG::rng<uint64_t>(1000000);
    mSink.dataSink.setHandler<TickerPrice>(
        [this](Span<TickerPrice> prices) { priceUpdate(prices); });
    mSink.dataSink.setHandler<OrderStatus>(
        [this](Span<OrderStatus> statuses) { orderUpdates(statuses); });
    mSink.controlSink.addCommandHandler(
        {TraderCommand::TradeStart, TraderCommand::TradeStop, TraderCommand::TradeSwitch,
         TraderCommand::CollectStats, TraderCommand::TradeSpeedUp, TraderCommand::TradeSpeedDown},
        [this](TraderCommand cmd) { processCommand(cmd); });
  }

private:
  void priceUpdate(Span<TickerPrice> prices) {
    spdlog::info("PriceUpdate: {}", [&prices] {
      std::string pricesUpdate;
      for (auto price : prices) {
        pricesUpdate += utils::toString(price) + " ";
      }
      return pricesUpdate;
    }());
  }

  void orderUpdates(Span<OrderStatus> statuses) {
    mStats.ordersClosed.fetch_add(statuses.size());
    for (auto &status : statuses) {
      auto rtt = RttTracker::logRtt(status.id);
      spdlog::info(
          "{} RTT {}", [&status] { return utils::toString(status); }(),
          [&rtt] { return utils::getScaleUs(rtt); }());
      (status.action == OrderAction::Buy)
          ? mStats.balance.fetch_add(status.fillPrice * status.quantity)
          : mStats.balance.fetch_sub(status.fillPrice * status.quantity);
    }
  }

  void tradeSwitch(bool start) {
    mTrading.store(start);
    if (mTrading) {
      tradeCycle();
    } else {
      mTimer.cancel();
    }
  }

  void tradeSomething() {
    auto tickerPrice = mPrices[utils::RNG::rng(mPrices.size() - 1)];
    Order order;
    order.id = utils::getLinuxTimestamp();
    order.ticker = tickerPrice.ticker;
    order.price = utils::RNG::rng<uint32_t>(tickerPrice.price * 2);
    order.action = utils::RNG::rng(1) == 0 ? OrderAction::Buy : OrderAction::Sell;
    order.quantity = utils::RNG::rng(1000);
    spdlog::debug("Placing order {}", [&order] { return utils::toString(order); }());
    mSink.ioSink.post(Span<Order>{&order, 1});
  }

  void processCommand(TraderCommand cmd) {
    switch (cmd) {
    case TraderCommand::TradeStart:
      tradeSwitch(true);
      break;
    case TraderCommand::TradeStop:
      tradeSwitch(false);
      break;
    case TraderCommand::TradeSwitch:
      tradeSwitch(!mTrading);
      break;
    case TraderCommand::CollectStats:
      mSink.controlSink.onEvent(mStats);
      break;
    case TraderCommand::TradeSpeedUp:
      if (mTradeRate > 50) {
        mTradeRate.store(mTradeRate / 2);
      }
      spdlog::info("Trade rate: {}", [this] { return utils::getScaleUs(mTradeRate); }());
      break;
    case TraderCommand::TradeSpeedDown:
      mTradeRate.store(mTradeRate * 2);
      spdlog::info("Trade rate: {}", [this] { return utils::getScaleUs(mTradeRate); }());
      break;
    default:
      break;
    }
  }

private:
  void tradeCycle() {
    if (!mTrading.load()) {
      return;
    }
    mTimer.expires_after(Microseconds(mTradeRate));
    mTimer.async_wait([this](BoostErrorRef ec) {
      if (!ec) {
        tradeSomething();
        tradeCycle();
      }
    });
  }

private:
  TraderSink &mSink;
  std::vector<TickerPrice> mPrices;

  TradingStats mStats;

  std::atomic<size_t> mTradeRate;
  std::atomic_bool mTrading;
  SteadyTimer mTimer;
};

} // namespace hft::trader

#endif // HFT_TRADER_BUYSOMESELLSOMESTRATEGY_HPP