#include "HCSNode.h"
#include <stdexcept>
#include <algorithm>
#include <cstdint>

// HCSNodeの実装に必要な具体的なトランスポートクラスと鍵プロバイダのインクルード
#include "hcs_net/ControlUdpTransport.h"
#include "hcs_net/QuicNgTcp2Transport.h"
#include "hcs_control/KeyProvider.h"

// 制御メッセージタイプ定義（簡易プロトコル）
// 実際にはProtocol Buffersなどを使用してデシリアライズを行う
#define MSG_TYPE_ADVERTISE 1
#define MSG_TYPE_HEARTBEAT 2

namespace hcs {

HCSNode::HCSNode(boost::asio::io_context& io_context, unsigned short media_port, unsigned short control_port)
    : io_context_(io_context), 
      media_port_(media_port), 
      control_port_(control_port)
{
    std::cout << "--- HCSNode Initialization ---\n";

    // 1. 制御層 (TopologyManager) の初期化
    topology_manager_ = std::make_unique<hcs_control::TopologyManager>();
    
    // 鍵プロバイダのインスタンス化 (トランスポート層の暗号化に必要)
    std::shared_ptr<hcs_control::KeyProvider> key_provider = std::make_shared<hcs_control::KeyProvider>();

    // 2. データパス層 (Encoder/Decoder) の初期化
    stream_encoder_ = std::make_shared<hcs_media::StreamEncoder>();
    stream_decoder_ = std::make_shared<hcs_media::StreamDecoder>();

    // 3. トランスポート層の初期化
    // Media Transport: IMediaTransportインターフェースをQuicNgTcp2Transportで実現 (セキュアUDP)
    // "127.0.0.1"はローカルアドレスのプレースホルダー
    media_transport_ = std::make_shared<hcs_net::QuicNgTcp2Transport>(
        io_context_, 
        key_provider, 
        "127.0.0.1", 
        media_port_
    );
    
    // Control Transport: IControlTransportインターフェースをControlUdpTransportで実現 (シンプルなUDP)
    control_transport_ = std::make_shared<hcs_net::ControlUdpTransport>(
        io_context_, 
        control_port_
    );

    std::cout << "Node configured for Media Port: " << media_port_
              << ", Control Port: " << control_port_ << std::endl;
}

void HCSNode::Start() {
    std::cout << "\n--- HCSNode Start Sequence ---\n";

    // 1. 制御層の起動
    topology_manager_->Start();
    std::cout << "Topology Manager started.\n";

    // 2. メディア・トランスポートの起動とデコーダへの接続
    // StreamDecoderはIMediaTransportに自身のHandleRtpPacketを登録し、受信パスを設定する
    stream_decoder_->StartDecoding(media_transport_);
    std::cout << "Media Decoder successfully connected to Media Transport's receive path.\n";

    // 3. 制御・トランスポートの起動とメッセージハンドラへの接続
    // Control Transportは受信したパケットをHCSNodeのHandleControlMessageに渡す
    control_transport_->StartReceive(
        [this](const std::vector<uint8_t>& message, const hcs_net::Endpoint& sender_endpoint) {
            this->HandleControlMessage(message, sender_endpoint);
        }
    );
    std::cout << "Control Transport started and connected to Control Message Handler.\n";

    // 4. エンコーダのトランスポート設定（送信パスの確立）
    // StreamEncoderは送信時にmedia_transport_を利用する
    stream_encoder_->SetTransport(media_transport_);
    std::cout << "Media Encoder successfully linked to Media Transport for sending.\n";

    // TODO: 初期ADVERTISEメッセージの送信ロジックを開始
    
    std::cout << "HCSNode is fully operational, waiting for I/O events.\n";
}

void HCSNode::Stop() {
    std::cout << "\n--- HCSNode Stop Sequence ---\n";
    
    // 1. 各トランスポート層の停止 (ソケットを閉じる)
    if (media_transport_) media_transport_->Stop();
    if (control_transport_) control_transport_->Stop();

    // 2. データパス層の停止 (FFmpegリソースの解放など)
    if (stream_encoder_) stream_encoder_->Stop();
    if (stream_decoder_) stream_decoder_->Stop();
    
    std::cout << "HCSNode components stopped and resources released.\n";
}

void HCSNode::HandleControlMessage(const std::vector<uint8_t>& message, const hcs_net::Endpoint& sender_endpoint) {
    // 制御メッセージの受信と初期ロギング
    std::cout << "[ControlHandler] Received " << message.size() << " bytes from "
              << sender_endpoint.address << ":" << sender_endpoint.port << std::endl;

    // 制御メッセージのデシリアライズとルーティング
    // 受信した生のメッセージデータ (message) をRouteControlMessageに渡す
    RouteControlMessage(message);
    
    // 実際のアプリケーションでは、送信元エンドポイント(sender_endpoint)の情報も
    // RouteControlMessageやTopologyManagerに渡してピアの状態を正確に更新する必要があります。
}

void HCSNode::RouteControlMessage(const std::vector<uint8_t>& message_data) {
    if (message_data.empty()) return;

    // メッセージタイプは最初の1バイトと仮定 (簡易的なデシリアライズ)
    uint8_t message_type = message_data[0];

    // **注意**: 以下のデシリアライズロジックはデモ目的のダミーであり、
    // 実際の実装ではバイナリプロトコルやシリアライザを使用する必要があります。

    switch (message_type) {
        case MSG_TYPE_ADVERTISE: {
            // AdvertiseMessage構造体を仮定してデシリアライズ
            // 受信パケットから情報を取り出す代わりにダミーデータを使用
            hcs_control::AdvertiseMessage adv_msg;
            // 本来は sender_endpoint からIPを取得すべきだが、ダミーで固定
            adv_msg.ip = "192.168.1.10"; 
            adv_msg.metrics.rtt_ms = 50; 
            adv_msg.metrics.hop_count = 1;
            adv_msg.groups = {"VideoGroup1", "PublicFeed"};

            topology_manager_->HandleAdvertise(adv_msg);
            std::cout << "[Router] Routed ADVERTISE message to TopologyManager (Metrics: RTT="
                      << adv_msg.metrics.rtt_ms << "ms, Groups: " << adv_msg.groups.size() << ").\n";
            break;
        }
        case MSG_TYPE_HEARTBEAT: {
            // 受信元IPとグループIDもパケットペイロードから取得すべき
            topology_manager_->HandleHeartbeat("192.168.1.10", "VideoGroup1");
            std::cout << "[Router] Routed HEARTBEAT message to TopologyManager.\n";
            break;
        }
        default:
            std::cerr << "[Router] Unknown control message type: " << (int)message_type << std::endl;
            break;
    }
}

} // namespace hcs
