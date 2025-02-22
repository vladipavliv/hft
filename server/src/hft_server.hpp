/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_SERVER_SERVER_HPP
#define HFT_SERVER_SERVER_HPP

#include "aggregator/aggregator.hpp"
#include "control_center/server_control_center.hpp"
#include "network/network_server.hpp"
#include "server_types.hpp"

namespace hft::server {

class HftServer {
public:
  HftServer() : mNetwork{mSink}, mCc{mSink}, mAggregator{mSink} {
    mSink.controlSink.addCommandHandler({ServerCommand::Shutdown}, [this](ServerCommand cmd) {
      if (cmd == ServerCommand::Shutdown) {
        stop();
      }
    });
  }

  void start() {
    spdlog::trace("Start HftServer");
    mSink.dataSink.start();
    mSink.controlSink.start();
    mNetwork.start();
    // Run io context along with other threads
    mSink.ioSink.start();
  }

  void stop() {
    spdlog::trace("Stop HftServer");
    mSink.ctx().stop();
    mSink.dataSink.stop();
  }

private:
  ServerSink mSink;
  network::NetworkServer mNetwork;
  Aggregator mAggregator;
  ServerControlCenter mCc;
};

} // namespace hft::server

#endif // HFT_SERVER_SERVER_HPP