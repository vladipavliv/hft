/**
 * @author Vladimir Pavliv
 * @date 2025-02-13
 */

#ifndef HFT_COMMON_ASYNCSOCKET_HPP
#define HFT_COMMON_ASYNCSOCKET_HPP

#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <vector>

#include "constants.hpp"
#include "network_types.hpp"
#include "types.hpp"
#include "utils/string_utils.hpp"

namespace hft {

/**
 * @brief Performs asynchronous reads and writes to a socket,
 */
template <typename SinkType, typename SerializerType, typename SocketType, typename MessageTypeIn>
class AsyncSocket {
public:
  using Type = AsyncSocket<SinkType, SerializerType, SocketType, MessageTypeIn>;
  using Sink = SinkType;
  using Serializer = SerializerType;
  using Socket = SocketType;
  using Endpoint = SocketType::endpoint_type;
  using MessageIn = MessageTypeIn;
  using UPtr = std::unique_ptr<Type>;

  AsyncSocket(Sink &sink, Socket &&socket)
      : mSink{sink}, mSocket{std::move(socket)}, mBuffer(BUFFER_SIZE),
        mId{utils::getTraderId(mSocket)} {
    mMessageBuffer.reserve(100);
    if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.set_option(boost::asio::socket_base::reuse_address{true});
    }
  }

  AsyncSocket(Sink &sink, Socket &&socket, Endpoint endpoint)
      : mSink{sink}, mSocket{std::move(socket)}, mEndpoint{std::move(endpoint)},
        mBuffer(BUFFER_SIZE) {
    mMessageBuffer.reserve(100);
    if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.set_option(boost::asio::socket_base::reuse_address{true});
    }
  }

  void asyncConnect() {
    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      mSocket.async_connect(mEndpoint, [this](BoostErrorRef ec) { connectHandler(ec); });
    } else if constexpr (std::is_same_v<Socket, UdpSocket>) {
      asyncRead();
    }
  }

  void asyncRead() {
    size_t writable = mBuffer.size() - mTail;
    uint8_t *writePtr = mBuffer.data() + mHead;

    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      mSocket.async_read_some(
          boost::asio::buffer(writePtr, writable),
          [this](BoostErrorRef code, size_t bytesRead) { readHandler(code, bytesRead); });
    } else if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.async_receive_from(
          boost::asio::buffer(writePtr, writable), mEndpoint,
          [this](BoostErrorRef code, size_t bytesRead) { readHandler(code, bytesRead); });
    }
  }

  template <typename MessageTypeOut>
  void asyncWrite(Span<MessageTypeOut> msgVec) {
    spdlog::trace("Socket::asyncWrite {} messages", msgVec.size());
    if (!mSocket.is_open()) {
      spdlog::error("Failed to write to the socket: not opened");
      return;
    }
    size_t allocSize = msgVec.size() * MAX_MESSAGE_SIZE;
    auto dataPtr = std::make_shared<ByteBuffer>(allocSize);

    size_t realSize{0};
    uint8_t *cursor = dataPtr->data();
    for (auto &msg : msgVec) {
      auto buffer = Serializer::serialize(msg);
      boost::endian::little_int16_at bodySize = static_cast<MessageSize>(buffer.size());

      std::memcpy(cursor, &bodySize, sizeof(bodySize));
      std::memcpy(cursor + sizeof(bodySize), buffer.data(), buffer.size());

      cursor += bodySize + sizeof(bodySize);
      realSize += bodySize + sizeof(bodySize);
    }

    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      boost::asio::async_write(
          mSocket, boost::asio::buffer(dataPtr->data(), realSize),
          [this, dataPtr](BoostErrorRef ec, size_t size) { writeHandler(ec, size); });
    } else if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.async_send_to(
          boost::asio::buffer(dataPtr->data(), realSize), mEndpoint,
          [this, dataPtr](BoostErrorRef ec, size_t size) { writeHandler(ec, size); });
    }
  }

  void retrieveTraderId() {
    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      if (mId == 0) {
        mId = utils::getTraderId(mSocket);
      }
    }
  }

  /**
   * @brief Just a quick way to distinguish between clients
   */
  inline TraderId getTraderId() const { return mId; }

private:
  void readHandler(BoostErrorRef ec, size_t bytesRead) {
    if (ec) {
      mHead = mTail = 0;
      if (ec != boost::asio::error::eof) {
        spdlog::error(ec.message());
      }
      return;
    }
    mTail += bytesRead;
    while (mHead + sizeof(MessageSize) < mTail) {
      uint8_t *cursor = mBuffer.data() + mHead;
      boost::endian::little_int16_at littleBodySize;
      std::memcpy(&littleBodySize, cursor, sizeof(littleBodySize));
      MessageSize bodySize = littleBodySize.value();
      if (mHead + sizeof(MessageSize) + bodySize > mTail) {
        // continue reading;
        break;
      }
      cursor += sizeof(MessageSize);
      auto result = Serializer::template deserialize<MessageIn>(cursor, bodySize);
      if (!result.ok()) {
        mHead = mTail = 0;
        break;
      }
      mMessageBuffer.emplace_back(result.value());
      if constexpr (!std::is_same_v<MessageTypeIn, TickerPrice>) {
        mMessageBuffer.back().traderId = getTraderId();
      }
      mHead += bodySize + sizeof(MessageSize);
    }
    if (mBuffer.size() - mTail < 256) {
      std::memmove(mBuffer.data(), mBuffer.data() + mHead, mTail - mHead);
      mTail = mTail - mHead;
      mHead = 0;
    }
    if (!mMessageBuffer.empty()) {
      mSink.dataSink.post(Span<MessageIn>(mMessageBuffer));
    }
    mMessageBuffer.clear();
    asyncRead();
  }

  void connectHandler(BoostErrorRef ec) {
    if (ec) {
      spdlog::error("Connect failed: {}", ec.message());
      return;
    }
    mSocket.set_option(TcpSocket::protocol_type::no_delay(true));
    asyncRead();
  }

  void writeHandler(BoostErrorRef ec, size_t written) {
    if (ec) {
      spdlog::error("Write failed: {}", ec.message());
    }
  }

private:
  Sink &mSink;
  Socket mSocket;
  Endpoint mEndpoint;

  size_t mHead{0};
  size_t mTail{0};
  ByteBuffer mBuffer;
  std::vector<MessageIn> mMessageBuffer;

  TraderId mId{};
};

} // namespace hft

#endif // HFT_COMMON_ASYNCSOCKET_HPP
