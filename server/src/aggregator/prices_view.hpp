/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_SERVER_MARKET_PRICESVIEW_HPP
#define HFT_SERVER_MARKET_PRICESVIEW_HPP

#include "aggregator_data.hpp"
#include "market_types.hpp"
#include "types.hpp"

namespace hft::server {

/**
 * @brief Provides view of the prices for read/write without memory overhead
 * Uses market data by const ref and the only thing it can change is the price
 */
class PricesView {
public:
  // Iterators at home
  class PriceIterator {
  public:
    PriceIterator(const AggregatorData &data) : mData{data}, mCursor{mData.begin()} {};

    void reset() { mCursor = mData.begin(); }

    PriceIterator &operator++() {
      if (mCursor != mData.end()) {
        mCursor++;
      }
      return *this;
    }

    PriceIterator operator++(int) {
      PriceIterator temp = *this;
      if (mCursor != mData.end()) {
        ++mCursor;
      }
      return temp;
    }

    TickerPrice operator*() {
      if (end()) {
        return TickerPrice{};
      }
      return TickerPrice{mCursor->first, mCursor->second.currentPrice};
    }

    bool end() { return mCursor == mData.end(); }

  private:
    const AggregatorData &mData;
    AggregatorData::const_iterator mCursor;
  };

  PricesView(const AggregatorData &data) : mData{data} {}

  Price getPrice(TickerRef ticker) {
    if (!mData.contains(ticker)) {
      spdlog::error("Ticker not found {}", [&ticker] { return utils::toStrView(ticker); }());
      return 0;
    }
    return mData.at(ticker).currentPrice;
  }

  void setPrice(const TickerPrice &price) {
    if (!mData.contains(price.ticker)) {
      spdlog::error("Ticker not found {}", [&price] { return utils::toStrView(price.ticker); }());
      return;
    }
    mData.at(price.ticker).currentPrice = price.price;
  }

  PriceIterator getPriceIterator() { return PriceIterator{mData}; }

private:
  const AggregatorData &mData;
};

} // namespace hft::server

#endif // HFT_SERVER_MARKET_PRICESVIEW_HPP