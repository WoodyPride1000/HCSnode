#include "hcs_media/StreamDecoder.h"
#include <iostream>
#include <boost/asio.hpp>
#include <vector>
#include <memory>
#include <algorithm>

// 依存するインターフェースのプロトタイプ（実際はヘッダーファイルに存在する）
namespace hcs_net {
    struct Endpoint {
        std::string address;
        uint16_t port;
    };

    // IMediaTransportは、暗号化/復号化されたパケットの送受信を抽象化
    class IMediaTransport {
    public:
        virtual ~IMediaTransport() = default;
        // 非同期受信のインターフェース
        virtual void AsyncReceiveFrom(
            std::vector<uint8_t>& buffer,
            Endpoint& sender_endpoint,
            std::function<void(const boost::system::error_code&, std::size_t)> handler
        ) = 0;
        // 実際にはもっと多くのインターフェースがある
    };
}

namespace hcs_media {

/**
 * @brief RTPパケットからペイロードとヘッダー情報を抽出する (疑似)
 * @param rtp_packet 受信したRTPパケットデータ
 * @param payload_offset RTPヘッダーサイズ（ここでは固定12バイトとする）
 * @return RTPペイロードへのポインタ
 */
const uint8_t* ParseRtpHeader(const std::vector<uint8_t>& rtp_packet, size_t& payload_offset) {
    if (rtp_packet.size() < 12) {
        // パケットがRTPヘッダーの最小長に満たない
        return nullptr;
    }

    // 実際のロジックでは、V, P, X, CC, M, PT, SeqNum, Timestamp, SSRCなどを解析する
    uint8_t version = (rtp_packet[0] >> 6) & 0x03; // バージョン (期待値: 2)
    uint8_t payload_type = rtp_packet[1] & 0x7F; // ペイロードタイプ (例: 96)

    // ここでは簡易的にRTPヘッダーを12バイトとして固定
    payload_offset = 12;

    std::cout << "[Decoder] RTP Packet received. Version: " << (int)version
              << ", PT: " << (int)payload_type << std::endl;

    return rtp_packet.data() + payload_offset;
}

// =========================================================================

StreamDecoder::StreamDecoder(
    boost::asio::io_context& io_context,
    std::shared_ptr<hcs_net::IMediaTransport> transport,
    const std::string& group_id
)
: io_context_(io_context),
  transport_(std::move(transport)),
  group_id_(group_id),
  // 受信バッファを最大MTU + 制御オーバーヘッド（例：2048バイト）で初期化
  receive_buffer_(2048)
{
    std::cout << "[Decoder] Initialized for group: " << group_id_ << std::endl;
    // FFmpeg/Libde265 デコーダコンテキストの初期化ロジックはここに入る
}

StreamDecoder::~StreamDecoder() {
    Stop();
}

void StreamDecoder::StartReceiving() {
    std::cout << "[Decoder] Starting continuous receive loop." << std::endl;
    // 最初の非同期受信オペレーションを開始
    ScheduleReceive();
}

void StreamDecoder::Stop() {
    std::cout << "[Decoder] Stopping decoder and cancelling transport receives." << std::endl;
    // トランスポート層に対して、このデコーダに関連する保留中の受信処理をキャンセルするよう依頼する
    // transport_->CancelReceives(this); // 実際にはキャンセルロジックが必要
}

void StreamDecoder::ScheduleReceive() {
    auto self = shared_from_this();

    // 1. トランスポート層に非同期受信を依頼
    // transport_->AsyncReceiveFrom のコールバック内で、自身 (self) を保持
    transport_->AsyncReceiveFrom(
        receive_buffer_,
        sender_endpoint_, // 受信元エンドポイント
        [self](const boost::system::error_code& ec, std::size_t bytes_received) {
            self->HandleReceive(ec, bytes_received);
        }
    );
}

// 受信処理のメインコールバック
void StreamDecoder::HandleReceive(const boost::system::error_code& ec, std::size_t bytes_received) {
    if (ec == boost::asio::error::operation_aborted) {
        // Stop() によってキャンセルされた
        return;
    }
    if (ec) {
        std::cerr << "[Decoder] Receive error: " << ec.message() << std::endl;
        // エラー後も復帰を試みるため、再スケジュール
        ScheduleReceive(); 
        return;
    }
    
    // 1. 受信データの復号化と検証
    // IMediaTransport層で既にAES-GCMによる復号化と認証タグの検証が行われていると想定。
    // bytes_received は復号化されたペイロードのサイズ。

    if (bytes_received > 0) {
        // 2. RTPパケットの処理とデコーダへの投入
        ProcessReceivedPacket(bytes_received);
    }

    // 3. 次のパケット受信をスケジュール
    ScheduleReceive();
}

void StreamDecoder::ProcessReceivedPacket(std::size_t bytes_received) {
    // 受信したデータサイズに合わせてバッファを調整
    std::vector<uint8_t> received_packet(
        receive_buffer_.begin(), 
        receive_buffer_.begin() + bytes_received
    );

    size_t payload_offset = 0;
    
    // 1. RTPヘッダー解析
    const uint8_t* rtp_payload = ParseRtpHeader(received_packet, payload_offset);

    if (rtp_payload) {
        // ペイロードデータサイズ
        size_t payload_size = bytes_received - payload_offset;

        std::cout << "[Decoder] Decrypted and parsed RTP. Payload size: " 
                  << payload_size << " bytes from " << sender_endpoint_.address << std::endl;

        // 2. 【FFmpeg/デコーダ連携箇所】
        // デコーダにペイロードを投入
        // 例: avcodec_send_packet()
        // 通常は、バッファリング、ジッタバッファ管理、RTPシーケンス番号チェックなどがここに入る。
        // DecodePacket(rtp_payload, payload_size);
        
        // 3. デコーダから出力されたフレーム (AVFrame) をレンダラーなどに渡す処理
        // HandleDecodedFrame(...);
    } else {
        std::cerr << "[Decoder] Error: Invalid RTP packet size or content." << std::endl;
    }
}

} // namespace hcs_media
