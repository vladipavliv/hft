/**
 * @author Vladimir Pavliv
 * @date 2025-02-10
 */

#ifndef HFT_SERVER_NETWORK_EGRESSSERVER_HPP
#define HFT_SERVER_NETWORK_EGRESSSERVER_HPP

#include <unordered_map>

#include "config/config.hpp"
#include "server_types.hpp"
#include "types/market_types.hpp"
#include "types/network_types.hpp"
#include "types/types.hpp"
#include "utils/utils.hpp"

namespace hft::server::network {

/**
 * @brief Accepts incoming requests for sending replies to the client
 */
class EgressServer {
public:
  using Socket = ServerSocket<TcpSocket, Order>;

  EgressServer(ServerSink &sink)
      : mSink{sink}, mAcceptor{mSink.ctx()}, mPort{Config::cfg.portTcpOut} {
    mSink.ioSink.setHandler<OrderStatus>([this](Span<OrderStatus> statuses) { send(statuses); });
  }

  void start() {
    TcpEndpoint endpoint(Tcp::v4(), mPort);
    mAcceptor.open(endpoint.protocol());
    mAcceptor.bind(endpoint);
    mAcceptor.listen();
    acceptConnection();
  }

  template <typename MessageType>
  void send(Span<MessageType> messages) {
    spdlog::trace("EgressServer::send {} messages", messages.size());
    std::sort(messages.begin(), messages.end(), TraderIdCmp<MessageType>{});

    auto [subSpan, leftover] = frontSubspan(messages, TraderIdCmp<MessageType>{});
    while (!subSpan.empty()) {
      auto conn = mConnections.find(subSpan.front().traderId);
      if (conn != mConnections.end()) {
        conn->second->asyncWrite(subSpan);
      } else {
        spdlog::trace("Trader {} is offline", [&subSpan] { return subSpan.front().traderId; }());
      }
      std::tie(subSpan, leftover) = frontSubspan(leftover, TraderIdCmp<MessageType>{});
    }
  }

private:
  void acceptConnection() {
    mAcceptor.async_accept([this](BoostErrorRef ec, TcpSocket socket) {
      if (ec) {
        spdlog::error("Failed to accept connection: {}", ec.message());
        return;
      }
      auto conn = std::make_unique<Socket>(mSink, std::move(socket));
      conn->retrieveTraderId();
      spdlog::debug("{} connected", conn->getTraderId());
      mConnections.emplace(conn->getTraderId(), std::move(conn));
      acceptConnection();
    });
  }

private:
  ServerSink &mSink;
  TcpAcceptor mAcceptor;
  Port mPort;

  std::unordered_map<TraderId, Socket::UPtr> mConnections;
};
} // namespace hft::server::network

#endif // HFT_SERVER_NETWORK_EGRESSSERVER_HPP