#pragma once

#include <memory>
#include <string>
#include <boost/asio.hpp>
#include "hcs_net/TransportBase.h"          // Endpoint, IMediaTransport
#include "hcs_net/QuicNgTcp2Transport.h"    // トランスポート実装の例
#include "hcs_control/TopologyManager.h"    // トポロジー管理
#include "hcs_media/StreamEncoder.h"        // メディア送信
#include "hcs_media/StreamDecoder.h"        // メディア受信
#include "hcs_net/TransportAES256.h"        // KeyProvider

namespace hcs {

/**
 * @brief HCS自律分散型ノードのコアロジックを統合するクラス
 * 制御層、データパス層、トランスポート層の全コンポーネントを管理し、
 * ノードの起動・停止、ストリームのライフサイクル制御を行う。
 */
class HCSNode : public std::enable_shared_from_this<HCSNode> {
public:
    /**
     * @brief HCSNodeのコンストラクタ
     * @param io_context boost::asioのI/Oコンテキスト
     * @param self_node_id このノードの一意なID
     * @param local_addr ローカルリスニングアドレス (IPv6を想定)
     * @param local_port ローカルリスニングポート
     */
    HCSNode(
        boost::asio::io_context& io_context,
        const std::string& self_node_id,
        const std::string& local_addr,
        uint16_t local_port
    );
    
    ~HCSNode();

    /**
     * @brief ノードの全コンポーネントを起動し、トポロジー管理とメディアストリームを開始する
     * @param key_provider マスターキーを提供するプロバイダ
     */
    void Start(std::shared_ptr<hcs_net::KeyProvider> key_provider);

    /**
     * @brief ノードの全操作を停止し、リソースを解放する
     */
    void Stop();

private:
    boost::asio::io_context& io_context_;
    std::string self_node_id_;
    hcs_net::Endpoint self_endpoint_;

    // --- コンポーネント群 ---
    
    // 1. トランスポート層 (QUIC/Secure UDP)
    std::shared_ptr<hcs_net::IMediaTransport> media_transport_;
    
    // 2. 制御層 (トポロジー管理)
    std::shared_ptr<hcs_control::TopologyManager> topology_manager_;
    
    // 3. データパス層 (エンコーダ/デコーダ)
    std::shared_ptr<hcs_media::StreamEncoder> stream_encoder_;
    std::shared_ptr<hcs_media::StreamDecoder> stream_decoder_;

    // --- 内部ヘルパー関数 ---
    
    /**
     * @brief トポロジーマネージャから最適なピアを取得し、エンコーダの送信先を決定する
     */
    void SelectAndStartStream();

    /**
     * @brief デコーダをトランスポート層に接続し、受信を開始する
     */
    void StartMediaReception();
    
    /**
     * @brief トランスポート層の初期化を行う
     * @param key_provider マスターキープロバイダ
     */
    void InitTransport(std::shared_ptr<hcs_net::KeyProvider> key_provider);
};

} // namespace hcs
