#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <iostream>
#include <cstdint>
#include "IControlTransport.h" // IControlTransport

namespace hcs_net {

/**
 * @brief 制御メッセージのためのシンプルなUDPトランスポートの実装。
 * IControlTransportインターフェースを実装し、Boost.Asioを使ってUDP通信を行う。
 */
class ControlUdpTransport : public IControlTransport,
                            public std::enable_shared_from_this<ControlUdpTransport> {
public:
    /**
     * @brief コンストラクタ
     * @param io Boost.Asio I/Oコンテキスト
     * @param port リッスンするポート番号
     */
    ControlUdpTransport(boost::asio::io_context& io, uint16_t port)
        : io_(io), socket_(io), port_(port) {}

    /**
     * @brief 受信を開始し、ハンドラを登録する。
     */
    void StartReceive(RecvHandler handler) override {
        handler_ = std::move(handler);
        boost::asio::ip::udp::endpoint ep(boost::asio::ip::udp::v4(), port_);
        boost::system::error_code ec;
        socket_.open(ep.protocol(), ec);
        socket_.bind(ep, ec);
        if (ec) {
            std::cerr << "[ControlTransport] Bind error: " << ec.message() << "\n";
            return;
        }
        std::cout << "[ControlTransport] Listening on port " << port_ << ".\n";
        AsyncReceive();
    }

    /**
     * @brief 制御メッセージを非同期的に送信する。
     */
    void AsyncSendTo(const std::vector<uint8_t>& message,
                     const Endpoint& dest,
                     SendCallback on_sent = nullptr) override {
        // 実際にはBoost.Asioのasync_send_toロジックが入る
        std::cout << "[ControlTransport] Simulated sending " << message.size() 
                  << " bytes to " << dest.address << ":" << dest.port << std::endl;
        if (on_sent) io_.post([on_sent]() { on_sent(boost::system::error_code{}, 0); });
    }

    /**
     * @brief トランスポート層を停止する。
     */
    void Stop() override {
        boost::system::error_code ec;
        socket_.close(ec);
    }

private:
    boost::asio::io_context& io_;
    boost::asio::ip::udp::socket socket_;
    uint16_t port_;
    RecvHandler handler_;
    std::vector<uint8_t> recv_buffer_{4096};
    boost::asio::ip::udp::endpoint sender_endpoint_;

    /**
     * @brief 非同期受信ループ
     */
    void AsyncReceive() {
        socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_),
            sender_endpoint_,
            [self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_recvd) {
                if (!ec && bytes_recvd > 0) {
                    // 受信データをハンドラに渡す
                    if (self->handler_) {
                        std::vector<uint8_t> message(self->recv_buffer_.begin(), self->recv_buffer_.begin() + bytes_recvd);
                        hcs_net::Endpoint sender_ep{self->sender_endpoint_.address().to_string(), self->sender_endpoint_.port()};
                        self->handler_(message, sender_ep);
                    }
                }
                if (!ec) self->AsyncReceive();
            });
    }
};

} // namespace hcs_net
