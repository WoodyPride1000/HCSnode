#include "HCSNode.h"
#include <iostream>
#include <sstream>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>

namespace hcs {

HCSNode::HCSNode(
    boost::asio::io_context& io_context,
    const std::string& self_node_id,
    const std::string& local_addr,
    uint16_t local_port
) :
    io_context_(io_context),
    self_node_id_(self_node_id),
    self_endpoint_(local_addr, local_port)
{
    std::cout << "[HCSNode] Initializing HCSNode: ID=" << self_node_id_ 
              << ", Endpoint=" << self_endpoint_.address << ":" << self_endpoint_.port << std::endl;
}

HCSNode::~HCSNode() {
    Stop();
    std::cout << "[HCSNode] HCSNode destroyed.\n";
}

void HCSNode::Start(std::shared_ptr<hcs_net::KeyProvider> key_provider) {
    if (!key_provider) {
        throw std::runtime_error("[HCSNode] Failed to start: KeyProvider is null.");
    }

    std::cout << "[HCSNode] Starting all components...\n";

    // 1. トランスポート層の初期化 (メディアデータ用)
    InitTransport(key_provider);

    // 2. 制御層 (TopologyManager) の初期化
    // TopologyManagerは、自ノードの情報を登録し、制御トランスポートを設定する
    std::cout << "[HCSNode] Initializing Topology Manager...\n";
    topology_manager_ = std::make_shared<hcs_control::TopologyManager>(
        io_context_, 
        self_node_id_, 
        self_endpoint_
    );

    // 3. データパス層 (Encoder/Decoder) の初期化
    // Encoderはダミーメディアソースを使用し、Transportにデータを渡す
    std::cout << "[HCSNode] Initializing Stream Encoder and Decoder...\n";
    stream_encoder_ = std::make_shared<hcs_media::StreamEncoder>(io_context_);
    stream_decoder_ = std::make_shared<hcs_media::StreamDecoder>(io_context_);
    
    // 4. コンポーネント間の接続
    
    // a) DecoderをTransportに接続: トランスポート層で復号されたパケットをDecoderが受け取る
    media_transport_->SetReceiveCallback(
        [this](const std::vector<uint8_t>& decrypted_data, const hcs_net::Endpoint& sender) {
            // 受信したデータをStreamDecoderに渡す
            this->stream_decoder_->HandleDecryptedPacket(decrypted_data, sender);
        }
    );

    // b) EncoderをTransportに接続: EncoderがエンコードしたパケットをTransportが送信する
    stream_encoder_->SetSendCallback(
        [this](const std::vector<uint8_t>& encrypted_data) {
            // TopologyManagerから最適な宛先を取得して送信する (今回はダミー)
            this->media_transport_->Send(encrypted_data, this->self_endpoint_); // 仮に自分宛に送る
        }
    );
    
    // 5. ストリーム受信を開始
    StartMediaReception();

    // 6. 制御メッセージの受信を開始
    // TopologyManagerは内部で制御用UDPソケットを開き、ADVERTISE/JOINなどの制御パケットを待ち受ける
    // topology_manager_->StartControlReception(); // (実装はTopologyManager側)

    // 7. ストリーム送信を開始 (この例ではトポロジー解決をスキップし、即時開始)
    SelectAndStartStream(); 
    
    std::cout << "[HCSNode] All components started successfully.\n";
}

void HCSNode::Stop() {
    std::cout << "[HCSNode] Stopping all operations...\n";
    // 停止は逆順に行うのが一般的 (データパス -> 制御 -> トランスポート)
    
    if (stream_encoder_) {
        stream_encoder_->Stop();
        stream_encoder_.reset();
    }
    if (stream_decoder_) {
        stream_decoder_->Stop();
        stream_decoder_.reset();
    }
    
    if (topology_manager_) {
        topology_manager_->Stop();
        topology_manager_.reset();
    }
    
    if (media_transport_) {
        media_transport_->Stop();
        media_transport_.reset();
    }
    
    std::cout << "[HCSNode] All components stopped.\n";
}

void HCSNode::InitTransport(std::shared_ptr<hcs_net::KeyProvider> key_provider) {
    std::cout << "[HCSNode] Initializing Media Transport (QUIC/Secure UDP)...\n";
    
    // トランスポート実装のインスタンス化 (QuicNgTcp2Transport を使用)
    // 制御メッセージ用のポートはTopologyManagerが別途持つため、ここではメディアポートのみを扱う
    media_transport_ = std::make_shared<hcs_net::QuicNgTcp2Transport>(
        io_context_,
        self_endpoint_,
        key_provider // 鍵プロバイダをTransportに渡す
    );

    // Transportの起動
    media_transport_->Start();
    std::cout << "[HCSNode] Media Transport started on " 
              << self_endpoint_.address << ":" << self_endpoint_.port << std::endl;
}

void HCSNode::StartMediaReception() {
    std::cout << "[HCSNode] Starting Media Stream Reception (Decoder)...\n";
    // デコーダは、Transportの受信コールバックを通じてデータを自動的に受け取るため、
    // ここでは特にブロックする操作はない。
    // 必要に応じてデコーダに初期設定を渡す
    stream_decoder_->Start();
}

void HCSNode::SelectAndStartStream() {
    std::cout << "[HCSNode] Selecting optimal peer and starting stream transmission...\n";
    
    // *** 実際の動作 ***
    // 1. TopologyManagerに、要求するメディアストリームのプロファイル (e.g., "Camera_Feed_High_Res") を問い合わせる
    //    auto optimal_peer = topology_manager_->FindOptimalPeer("Camera_Feed_High_Res");
    // 2. optimal_peer.endpoint を取得
    // 3. Encoderにこのエンドポイントを送信先として設定し、送信を開始させる
    //    stream_encoder_->SetDestination(optimal_peer.endpoint);
    
    // *** シミュレーション/初期フェーズ ***
    // 今回はトポロジー解決をスキップし、エンコーダのダミーストリームを即時開始
    stream_encoder_->Start();
    
    std::cout << "[HCSNode] Stream Encoder started. Publishing dummy media data.\n";
}

} // namespace hcs
