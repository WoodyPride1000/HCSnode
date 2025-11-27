#include "hcs_media/StreamEncoder.h"
#include <iostream>
#include <random>
#include <chrono>

namespace hcs_media {

// 疑似RTPパケットを生成するヘルパー関数
// (実際はFFmpegのAVPacketをRTPパケットに変換するロジックが入る)
std::vector<uint8_t> CreateDummyRtpPacket(size_t frame_size) {
    // 最小限のRTPヘッダー (12バイト) + ダミーペイロード
    // V=2, P=0, X=0, CC=0, M=0, PT=96 (Dynamic), SeqNum, Timestamp, SSRC
    std::vector<uint8_t> packet(12 + frame_size);
    
    // 0: V=2, P=0, X=0, CC=0
    packet[0] = 0x80;
    // 1: PT=96 (ペイロードタイプ)
    packet[1] = 96;
    // 2-3: シーケンス番号 (ここではダミー値を設定)
    packet[2] = 0x00;
    packet[3] = 0x01;
    // 4-7: タイムスタンプ
    packet[4] = 0x00;
    packet[5] = 0x00;
    packet[6] = 0x00;
    packet[7] = 0x00;
    // 8-11: SSRC
    packet[8] = 0xDE;
    packet[9] = 0xAD;
    packet[10] = 0xBE;
    packet[11] = 0xEF;

    // ダミーペイロードの充填
    // (実際はFFmpegエンコーダからのH.265/VP9 NALUが入る)
    std::fill(packet.begin() + 12, packet.end(), 0xAA); 
    
    return packet;
}

StreamEncoder::StreamEncoder(
    boost::asio::io_context& io_context,
    std::shared_ptr<hcs_net::IMediaTransport> transport,
    const hcs_net::Endpoint& dest_endpoint
)
: io_context_(io_context),
  transport_(std::move(transport)),
  dest_endpoint_(dest_endpoint),
  encoding_timer_(io_context)
{
    std::cout << "[Encoder] Initialized for destination: " 
              << dest_endpoint_.address << ":" << dest_endpoint_.port << std::endl;
    // FFmpegコンテキストの初期化ロジックはここに入る
}

StreamEncoder::~StreamEncoder() {
    Stop();
}

void StreamEncoder::StartPublishing() {
    std::cout << "[Encoder] Starting publishing loop." << std::endl;
    // 最初のタイマーを即座に設定
    // 1000/30 = 約33ミリ秒ごとにフレーム処理をシミュレート
    encoding_timer_.expires_from_now(boost::posix_time::milliseconds(33)); 
    encoding_timer_.async_wait(
        // async_waitのコールバックで shared_from_this を使用して自身をライフタイム管理
        [self = shared_from_this()](const boost::system::error_code& ec) {
            self->HandleEncodingTimer(ec);
        }
    );
}

void StreamEncoder::Stop() {
    std::cout << "[Encoder] Stopping encoder and canceling timer." << std::endl;
    // タイマーをキャンセルし、I/Oを停止する
    boost::system::error_code ec;
    encoding_timer_.cancel(ec);
    // トランスポート層の停止はHCSNode全体で管理されるが、個別に止めることもできる
    // transport_->Stop();
}

// エンコーディングと送信のメインループ駆動コールバック
void StreamEncoder::HandleEncodingTimer(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        // Stop() によってキャンセルされた
        return;
    }
    if (ec) {
        std::cerr << "[Encoder] Timer error: " << ec.message() << std::endl;
        return;
    }

    // 1. フレームの処理とRTPパケット化
    ProcessNextFrame(); 

    // 2. 次のフレーム処理をスケジュール
    // 33ミリ秒 (約 30 FPS)
    encoding_timer_.expires_at(encoding_timer_.expires_at() + boost::posix_time::milliseconds(33));
    encoding_timer_.async_wait(
        [self = shared_from_this()](const boost::system::error_code& next_ec) {
            self->HandleEncodingTimer(next_ec);
        }
    );
}

void StreamEncoder::ProcessNextFrame() {
    // 【FFmpeg 連携箇所】
    // 1. FFmpeg: avcodec_send_frame() -> avcodec_receive_packet() でAVPacket(エンコード済みNALU)を取得
    // 2. RTPパケット化: 取得したAVPacketをMTUサイズ(例:1400バイト)以下のRTPパケット群に分割

    // ダミーのRTPパケットを生成 (今回は単一の大きなパケットを想定)
    size_t dummy_frame_size = 1200; // 1200バイトのペイロードを想定
    std::vector<uint8_t> rtp_packet = CreateDummyRtpPacket(dummy_frame_size);
    
    std::cout << "[Encoder] Encoded frame (" << rtp_packet.size() << " bytes). Sending..." << std::endl;

    // 生成したRTPパケットをトランスポート層へ渡し、暗号化と非同期送信を実行させる
    SendRtpPacket(rtp_packet);
}

// 実際の非同期送信をトランスポート層に依頼する
void StreamEncoder::SendRtpPacket(const std::vector<uint8_t>& rtp_payload) {
    auto self = shared_from_this();
    
    // IMediaTransport::AsyncSendTo を呼び出す
    // トランスポート層（UdpTransport）が、ここで AES-GCM による暗号化を行う。
    transport_->AsyncSendTo(
        rtp_payload, 
        dest_endpoint_,
        [self](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                // 送信エラーをログ
                std::cerr << "[Encoder] Send error: " << ec.message() << std::endl;
            } else {
                // 成功ログ（暗号化後のサイズがbytesに入っている）
                std::cout << "[Encoder] Sent " << bytes << " encrypted bytes to " 
                          << self->dest_endpoint_.address << std::endl;
            }
        }
    );
}

} // namespace hcs_media
