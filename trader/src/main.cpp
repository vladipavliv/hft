/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#include "config/config.hpp"
#include "config/config_reader.hpp"
#include "logger.hpp"
#include "trader.hpp"
#include "utils/string_utils.hpp"

int main(int argc, char *argv[]) {
  using namespace hft;
  std::unique_ptr<trader::Trader> trader;
  try {
    Logger::initialize(spdlog::level::err, "trader_log.txt");
    ConfigReader::readConfig("trader_config.ini");

    Logger::monitorLogger->info("Trader configuration:");
    Config::cfg.logConfig();
    Logger::monitorLogger->info("LogLevel:{}", utils::toString(spdlog::get_level()));

    trader = std::make_unique<trader::Trader>();
    trader->start();
  } catch (const std::exception &e) {
    Logger::monitorLogger->critical("Exception caught in main {}", e.what());
    spdlog::critical("Exception caught in main {}", e.what());
    spdlog::default_logger()->flush();
    trader->stop();
  }
  return 0;
}