/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#include "config/config.hpp"
#include "config/config_reader.hpp"
#include "hft_server.hpp"
#include "logger_manager.hpp"
#include "template_types.hpp"
#include "utils/string_utils.hpp"

int main() {
  try {
    using namespace hft;
    LoggerManager::initialize(LoggerManager::LoggerMode::Console, spdlog::level::trace);
    ConfigReader::readConfig("server_config.ini");

    spdlog::info("Server configuration:");
    Config::cfg.logConfig();
    spdlog::info("LogLevel:{}", utils::toString(spdlog::get_level()));

    server::HftServer server;
    server.start();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  }
  return 0;
}