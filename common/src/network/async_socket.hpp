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

#include "boost_types.hpp"
#include "constants.hpp"
#include "network_types.hpp"
#include "pool/buffer_pool.hpp"
#include "serialization/flat_buffers/fb_serializer.hpp"
#include "types.hpp"
#include "utils/string_utils.hpp"

namespace hft {

template <typename SocketType, typename MessageTypeIn>
class AsyncSocket {
public:
  using Type = AsyncSocket<SocketType, MessageTypeIn>;
  using Socket = SocketType;
  using Endpoint = Socket::endpoint_type;
  using MessageIn = MessageTypeIn;
  using UPtr = std::unique_ptr<Type>;
  using Serializer = serialization::FlatBuffersSerializer;
  using MsgHandler = CRefHandler<MessageIn>;

  AsyncSocket(Socket &&socket, TraderId id = 0, MsgHandler handler = MsgHandler{})
      : mSocket{std::move(socket)}, mId{id}, mHandler{std::move(handler)},
        mReadBuffer(BUFFER_SIZE) {
    if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.set_option(boost::asio::socket_base::reuse_address{true});
    }
  }

  AsyncSocket(Socket &&socket, Endpoint endpoint, MsgHandler handler = MsgHandler{})
      : AsyncSocket(std::move(socket), 0, handler) {
    mEndpoint = std::move(endpoint);
  }

  void asyncConnect(Callback callback = Callback()) {
    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      mSocket.async_connect(mEndpoint, [this, callback](BoostErrorRef ec) {
        if (ec) {
          spdlog::error("Failed to connect socket:{}", ec.message());
          return;
        }
        mSocket.set_option(TcpSocket::protocol_type::no_delay(true));
        if (callback) {
          callback();
        }
      });
    } else if constexpr (std::is_same_v<Socket, UdpSocket>) {
      asyncRead();
    }
  }

  void asyncRead() {
    size_t writable = mReadBuffer.size() - mTail;
    uint8_t *writePtr = mReadBuffer.data() + mTail;

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
    size_t allocSize = msgVec.size() * MAX_SERIALIZED_MESSAGE_SIZE;
    auto dataPtr = std::make_unique<ByteBuffer>(allocSize);

    size_t totalSize{0};
    uint8_t *cursor = dataPtr->data();
    for (auto &msg : msgVec) {
      auto msgSize = serializeMessage(msg, cursor);
      cursor += msgSize;
      totalSize += msgSize;
    }

    if constexpr (std::is_same_v<Socket, TcpSocket>) {
      boost::asio::async_write(mSocket, boost::asio::buffer(dataPtr->data(), totalSize),
                               [this, data = std::move(dataPtr)](BoostErrorRef ec, size_t size) {
                                 if (ec) {
                                   spdlog::error("Write failed: {}", ec.message());
                                 }
                               });
    } else if constexpr (std::is_same_v<Socket, UdpSocket>) {
      mSocket.async_send_to(boost::asio::buffer(dataPtr->data(), totalSize), mEndpoint,
                            [this, data = std::move(dataPtr)](BoostErrorRef ec, size_t size) {
                              if (ec) {
                                spdlog::error("Write failed: {}", ec.message());
                              }
                            });
    }
  }

private:
  template <typename Type>
  boost::endian::little_int16_at serializeMessage(Type &msg, uint8_t *cursor) {
    auto buffer = Serializer::serialize(msg);
    boost::endian::little_int16_at bodySize = static_cast<MessageSize>(buffer.size());

    std::memcpy(cursor, &bodySize, sizeof(bodySize));
    std::memcpy(cursor + sizeof(bodySize), buffer.data(), buffer.size());

    return bodySize + sizeof(bodySize);
  }

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
      uint8_t *cursor = mReadBuffer.data() + mHead;
      boost::endian::little_int16_at littleBodySize = 0;
      std::memcpy(&littleBodySize, cursor, sizeof(littleBodySize));
      MessageSize bodySize = littleBodySize.value();
      if (mHead + sizeof(MessageSize) + bodySize > mReadBuffer.size()) {
        rotateBuffer();
        break;
      }
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
      if constexpr (!std::is_same_v<MessageTypeIn, TickerPrice>) {
        result.value.traderId = mId;
      }
      mHandler(result.value);
      mHead += bodySize + sizeof(MessageSize);
    }
    if (mReadBuffer.size() - mTail < 256) {
      rotateBuffer();
    }
    asyncRead();
  }

  void rotateBuffer() {
    std::memmove(mReadBuffer.data(), mReadBuffer.data() + mHead, mTail - mHead);
    mTail = mTail - mHead;
    mHead = 0;
  }

private:
  Socket mSocket;
  Endpoint mEndpoint;
  MsgHandler mHandler;

  size_t mHead{0};
  size_t mTail{0};
  ByteBuffer mReadBuffer;
  TraderId mId{};
};

} // namespace hft

#endif // HFT_COMMON_ASYNCSOCKET_HPP
