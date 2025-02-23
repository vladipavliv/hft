/**
 * @author Vladimir Pavliv
 * @date 2025-02-10
 */

#ifndef HFT_SERVER_NETWORK_BROADCASTSERVER_HPP
#define HFT_SERVER_NETWORK_BROADCASTSERVER_HPP

#include <spdlog/spdlog.h>
#include <unordered_map>

#include "boost_types.hpp"
#include "config/config.hpp"
#include "server_types.hpp"
#include "types/market_types.hpp"
#include "types/network_types.hpp"
#include "types/types.hpp"
#include "utils/utils.hpp"

namespace hft::server::network {

class BroadcastServer {
public:
  using Socket = ServerSocket<UdpSocket, TickerPrice>;

  BroadcastServer(ServerSink &sink)
      : mSink{sink}, mSocket{mSink, createSocket(sink.ctx()),
                             UdpEndpoint{Ip::address_v4::broadcast(), Config::cfg.portUdp}} {
    mSink.ioSink.setHandler<TickerPrice>(
        [this](Span<TickerPrice> prices) { mSocket.asyncWrite(prices); });
  }

  void start() { /*spdlog::debug("Start broadcasting market data at {}", Config::cfg.portUdp);*/ }

private:
  UdpSocket createSocket(IoContext &ctx) {
    UdpSocket socket(ctx, Udp::v4());
    socket.set_option(boost::asio::socket_base::broadcast(true));
    return socket;
  }

  ServerSink &mSink;
  Socket mSocket;
};
} // namespace hft::server::network

#endif // HFT_SERVER_NETWORK_EGRESSSERVER_HPP