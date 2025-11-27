#pragma once

#include "common.h"
#include <boost/asio.hpp> // Boost.Asioの使用を想定
#include <iostream>
#include <memory> // shared_from_this を利用

/**
 * @namespace hcs_net
 * @brief HCSノードのネットワーク関連コンポーネントを格納する名前空間
 */
namespace hcs_net {

// Boost.Asioの名前空間を簡略化
namespace asio = boost::asio;
using UdpSocket = asio::ip::udp::socket;
using UdpEndpoint = asio::ip::udp::endpoint;
using IoContext = asio::io_context;

/**
 * @brief UDPを使用したトランスポート層の具象実装
 * * このクラスは、hcs_net::Transport インターフェースを実装し、
 * 実際には Boost.Asio を使用して非同期なUDP通信を処理することを想定しています。
 */
class UdpTransport : public Transport, public std::enable_shared_from_this<UdpTransport> {
public:
    /**
     * @brief コンストラクタ
     * @param io_context AsioのI/Oコンテキスト
     * @param local_port バインドするローカルポート番号
     */
    UdpTransport(IoContext& io_context, unsigned short local_port)
        : io_context_(io_context),
          socket_(io_context, UdpEndpoint(asio::ip::udp::v4(), local_port)) 
    {
        // 受け入れバッファの初期化
        receive_buffer_.resize(65536); // 最大UDPパケットサイズに設定
    }

    /**
     * @brief トランスポート層の起動（非同期受信の開始）
     */
    void Start() override {
        // 非同期受信処理を開始
        StartReceive();
        std::cout << "UdpTransport started on port " << socket_.local_endpoint().port() << std::endl;
    }

    /**
     * @brief トランスポート層の停止
     */
    void Stop() override {
        if (socket_.is_open()) {
            boost::system::error_code ec;
            socket_.close(ec);
            if (ec) {
                std::cerr << "Error closing UDP socket: " << ec.message() << std::endl;
            }
            std::cout << "UdpTransport stopped." << std::endl;
        }
    }

    /**
     * @brief データを送信する
     * @param data 送信するバイトベクター
     * @param destination 宛先エンドポイント (hcs_net::Endpoint)
     */
    void Send(const std::vector<uint8_t>& data, const Endpoint& destination) override {
        // hcs_net::Endpointをasio::ip::udp::endpointに変換
        asio::ip::address address = asio::ip::make_address(destination.ip);
        UdpEndpoint asio_endpoint(address, destination.port);

        // 非同期送信。ハンドラで宛先とデータサイズをキャプチャし、ログ出力に使用する
        socket_.async_send_to(
            asio::buffer(data),
            asio_endpoint,
            // dest に destination.ToString() の結果をコピーして、エラー時のログに使用
            [data_size = data.size(), dest = destination.ToString()](boost::system::error_code ec, std::size_t transferred) {
                if (ec) {
                    // エラー発生時、宛先とエラーメッセージを出力
                    std::cerr << "UDP Send failed to " << dest << ": " << ec.message() << std::endl;
                } else if (transferred != data_size) {
                    // UDPでは通常発生しないが、念のため部分送信の警告を出力
                    std::cerr << "Warning: Only " << transferred << "/" << data_size << " bytes transferred to " << dest << std::endl;
                } else {
                    // 送信成功ログはここでは省略（デバッグ時に有効化するとよい）
                    // std::cout << "UDP Send success to " << dest << ": " << transferred << " bytes." << std::endl;
                }
            });
    }

    /**
     * @brief 受信コールバックの設定
     */
    void SetReceiveCallback(ReceiveCallback callback) override {
        receive_callback_ = std::move(callback);
    }
    
    /**
     * @brief 受信コールバックの設定 (ムーブ)
     */
    void SetReceiveCallback(ReceiveCallback&& callback) override {
        receive_callback_ = std::move(callback);
    }

private:
    IoContext& io_context_;
    UdpSocket socket_;
    ReceiveCallback receive_callback_;
    UdpEndpoint remote_endpoint_; // 受信時の送信元を保持
    std::vector<uint8_t> receive_buffer_; // 受信バッファ

    /**
     * @brief 非同期受信を開始する
     */
    void StartReceive() {
        // socket_.async_receive_from のためのキャプチャが安全であることを保証するため
        // shared_from_this を使用してインスタンスの寿命を延ばす
        socket_.async_receive_from(
            asio::buffer(receive_buffer_),
            remote_endpoint_,
            [self = shared_from_this()](boost::system::error_code ec, std::size_t bytes_received) {
                self->HandleReceive(ec, bytes_received);
            });
    }

    /**
     * @brief 非同期受信完了時のハンドラ
     */
    void HandleReceive(const boost::system::error_code& ec, std::size_t bytes_received) {
        if (!ec) {
            // 受信成功
            std::vector<uint8_t> received_data(receive_buffer_.begin(), receive_buffer_.begin() + bytes_received);
            
            // hcs_net::Endpointに変換
            Endpoint sender_endpoint = {
                remote_endpoint_.address().to_string(), 
                remote_endpoint_.port()
            };

            // コールバックを呼び出し
            if (receive_callback_) {
                receive_callback_(received_data, sender_endpoint);
            }
            
            // 次の非同期受信を開始
            StartReceive();
        } else if (ec != asio::error::operation_aborted) {
            // 停止以外のエラー
            std::cerr << "UDP Receive error: " << ec.message() << std::endl;
            // エラーが発生しても、再度受信を開始する
            StartReceive(); 
        }
    }
};

} // namespace hcs_net
