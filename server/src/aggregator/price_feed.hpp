/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_SERVER_PRICEFEED_HPP
#define HFT_SERVER_PRICEFEED_HPP

#include <atomic>
#include <spdlog/spdlog.h>
#include <vector>

#include "boost_types.hpp"
#include "market_types.hpp"
#include "prices_view.hpp"
#include "server_types.hpp"
#include "utils/rng.hpp"
#include "utils/utils.hpp"

namespace hft::server {

class PriceFeed {
public:
  PriceFeed(ServerSink &sink, PricesView prices)
      : mSink{sink}, mPrices{std::move(prices)}, mTimer{mSink.ctx()} {
    mSink.controlSink.addCommandHandler({ServerCommand::PriceFeedStart,
                                         ServerCommand::PriceFeedStop,
                                         ServerCommand::PriceFeedSwitch},
                                        [this](ServerCommand command) {
                                          if (command == ServerCommand::PriceFeedStart) {
                                            switchMode(true);
                                          } else if (command == ServerCommand::PriceFeedStop) {
                                            switchMode(false);
                                          } else if (command == ServerCommand::PriceFeedSwitch) {
                                            switchMode(!mShow);
                                          }
                                        });
  }

private:
  void switchMode(bool show) {
    mShow = show;
    if (mShow) {
      schedulePriceChange();
    } else {
      mShow.store(false);
      mTimer.cancel();
    }
  }

  void schedulePriceChange() {
    if (!mShow) {
      return;
    }
    mTimer.expires_after(Microseconds(Config::cfg.priceFeedRateUs));
    mTimer.async_wait([this](BoostErrorRef ec) {
      if (!ec) {
        updatePrice();
        schedulePriceChange();
      }
    });
  }

  void updatePrice() {
    // Iterate one ticker at a time and update its price
    static auto cursor = mPrices.getPriceIterator();
    if (cursor.end()) {
      cursor.reset();
    }
    auto tickerPrice = *cursor;
    mPrices.setPrice({tickerPrice.ticker, utils::RNG::rng<uint32_t>(900)});
    mSink.ioSink.post(*cursor);
    cursor++;
  }

private:
  ServerSink &mSink;
  PricesView mPrices;

  SteadyTimer mTimer;
  std::atomic_bool mShow{false};
};

} // namespace hft::server

#endif // HFT_SERVER_PRICEFEED_HPP
