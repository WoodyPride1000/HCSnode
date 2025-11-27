#pragma once

// 外部ライブラリの依存性
#include <ngtcp2/ngtcp2.h>
#include <openssl/ssl.h>
#include <boost/asio.hpp>
// プロジェクト内の依存性
#include "TransportAES256.h" // TransportCrypto, KeyProvider
#include "TransportBase.h"    // IMediaTransport, Endpoint (必須)

namespace hcs_net {

class QuicNgTcp2Transport : public IMediaTransport,
                           public std::enable_shared_from_this<QuicNgTcp2Transport> {
public:
    using RecvHandler = IMediaTransport::RecvHandler;
    using SendCallback = IMediaTransport::SendCallback;

    QuicNgTcp2Transport(boost::asio::io_context& io,
                        std::shared_ptr<KeyProvider> kp,
                        const std::string& local_addr,
                        uint16_t local_port)
        : io_(io),
          key_provider_(kp),
          local_addr_(local_addr),
          local_port_(local_port),
          socket_(io)
    {
        // 1. AES-GCM 暗号化の初期化 (現在のセキュアUDPの役割)
        auto mk = key_provider_->GetMasterKey();
        auto ms = key_provider_->GetMasterSalt();
        crypto_ = std::make_unique<TransportCrypto>(mk, ms, "quic");
        recv_buffer_.resize(4096);

        // 2. OpenSSL TLS 1.3 コンテキストの初期化 (将来のQUIC/TLS用)
        ssl_ctx_ = SSL_CTX_new(TLS_method());
        if (!ssl_ctx_) {
            std::cerr << "[QuicNgTcp2Transport] SSL_CTX_new failed.\n";
            return;
        }
        SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
        SSL_CTX_set_cipher_list(ssl_ctx_, "TLS_AES_256_GCM_SHA384");
        SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_TLSv1_2);
        // TODO: 証明書・秘密鍵をロード or 生成
    }

    ~QuicNgTcp2Transport() override {
        Stop(); 
        if (ssl_ctx_) SSL_CTX_free(ssl_ctx_);
    }

    void StartReceive(RecvHandler handler) override {
        handler_ = std::move(handler);
        OpenSocket();
        AsyncReceive();
        // ngtcp2 connection 初期化 (QUIC移行時に実装)
        InitNgTcp2Connection();
    }

    void AsyncSendTo(const std::vector<uint8_t>& plaintext,
                     const Endpoint& dest,
                     SendCallback on_sent = nullptr) override
    {
        // 現在はAES-GCM暗号化の上にQUICストリーム送信のフックを配置
        std::vector<uint8_t> cipher;
        // 1. 暗号化 (QUIC移行時は ngtcp2/TLS が担当)
        if (!crypto_->Encrypt(plaintext.data(), plaintext.size(), nullptr, 0, cipher)) {
            if (on_sent) io_.post([on_sent]() { on_sent(boost::asio::error::operation_aborted, 0); });
            return;
        }

        // 2. 送信 (QUIC移行時は ngtcp2 が UDP パケットを構築)
        SendQuicStream(dest, cipher, on_sent);
    }

    void Stop() override {
        boost::system::error_code ec;
        socket_.close(ec);
        // ngtcp2 関連のクリーンアップ
        if (quic_conn_) {
            ngtcp2_conn_close(quic_conn_, nullptr, 0);
            ngtcp2_conn_del(quic_conn_);
            quic_conn_ = nullptr;
        }
        if (ssl_) {
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
    }

private:
    boost::asio::io_context& io_;
    std::shared_ptr<KeyProvider> key_provider_;
    std::unique_ptr<TransportCrypto> crypto_;
    RecvHandler handler_;

    std::string local_addr_;
    uint16_t local_port_;
    boost::asio::ip::udp::socket socket_;
    std::vector<uint8_t> recv_buffer_;
    boost::asio::ip::udp::endpoint sender_endpoint_;

    // OpenSSL TLS
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;

    // ngtcp2 connection
    ngtcp2_conn* quic_conn_ = nullptr;
    uint64_t stream_id_ = 0; // メディアストリームID

    void OpenSocket() {
        boost::asio::ip::udp::endpoint ep(boost::asio::ip::address::from_string(local_addr_), local_port_);
        boost::system::error_code ec;
        socket_.open(ep.protocol(), ec);
        socket_.bind(ep, ec);
        if (ec) std::cerr << "[QuicNgTcp2Transport] Socket bind error: " << ec.message() << "\n";
    }

    void AsyncReceive() {
        socket_.async_receive_from(
            boost::asio::buffer(recv_buffer_),
            sender_endpoint_,
            [self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_recvd) {
                self->HandleReceive(ec, bytes_recvd);
            });
    }

    void HandleReceive(const boost::system::error_code& ec, std::size_t bytes_recvd) {
        if (!ec && bytes_recvd > 0) {
            // QUIC移行時のロジック:
            // ngtcp2_conn_recv(quic_conn_, ...) を呼び出し、ngtcp2_callbacks::recv_stream_data で
            // プレーンテキストを取得する。
            
            // 現在のロジック (AES-GCM セキュアUDP):
            std::vector<uint8_t> plaintext;
            if (crypto_->Decrypt(recv_buffer_.data(), bytes_recvd, nullptr, 0, plaintext)) {
                // 復号化されたRTPパケットをStreamDecoderへ渡す
                if (handler_) handler_(plaintext, Endpoint{sender_endpoint_.address().to_string(),
                                                          sender_endpoint_.port()});
            } else {
                std::cerr << "[QuicNgTcp2Transport] Decrypt/Auth failed on packet from " << sender_endpoint_.address().to_string() << "\n";
            }
        }
        if (!ec) AsyncReceive();
    }

    void InitNgTcp2Connection() {
        // TODO: QUIC コネクション作成、ハンドシェイク、TLS 連携の実装
        std::cout << "[QuicNgTcp2Transport] NgTcp2 connection initialization placeholder running.\n";
    }

    void SendQuicStream(const Endpoint& dest,
                        const std::vector<uint8_t>& data,
                        SendCallback on_sent)
    {
        // QUIC移行時のロジック:
        // 1. ngtcp2_conn_write_stream(quic_conn_, stream_id_, ...) でストリームデータ書き込み
        // 2. ngtcp2_conn_write_pkt(quic_conn_, ...) で送信すべき UDP パケットを生成
        // 3. 生成されたパケットを socket_.async_send_to で送信
        
        // 現在のロジック (AES-GCMで暗号化済みデータを直接UDP送信):
        socket_.async_send_to(
            boost::asio::buffer(data),
            boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(dest.address),
                                           dest.port),
            [on_sent](const boost::system::error_code& ec, std::size_t bytes_sent) {
                if (on_sent) on_sent(ec, bytes_sent);
            });
    }
};

} // namespace hcs_net
