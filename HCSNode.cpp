#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>
#include "HCSNode.h"
#include "hcs_net/TransportAES256.h" // KeyProviderの実装用

using namespace hcs;
using namespace hcs_net; // KeyProviderのため

/**
 * @brief HCSNodeのメイン実行関数。
 * @return 成功時は0
 */
int main() {
    try {
        // Boost.Asio I/Oコンテキストの準備
        boost::asio::io_context io_context;
        
        // シグナルハンドリングを設定し、Ctrl+C (SIGINT) でノードを停止できるようにする
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const boost::system::error_code&, int) {
            std::cout << "\n[Main] Interrupt received. Stopping I/O context...\n";
            io_context.stop();
        });

        // 1. ノードの設定
        std::string self_id = "Node_A123";
        std::string local_addr = "::1"; // IPv6ループバックアドレス
        uint16_t media_port = 8000;
        
        // 2. マスター鍵プロバイダの準備 (ダミー)
        // 実際には鍵管理サービスから取得する
        std::shared_ptr<KeyProvider> key_provider = std::make_shared<KeyProvider>(
            // ダミーの32バイトマスターキーとソルト
            std::vector<uint8_t>(32, 0xAA),
            std::vector<uint8_t>(16, 0xBB) 
        );

        // 3. HCSNodeのインスタンス化
        // HCSNode(io_context, self_node_id, local_addr, local_port) に合わせる
        std::shared_ptr<HCSNode> node = std::make_shared<HCSNode>(
            io_context, 
            self_id, 
            local_addr, 
            media_port
        );

        // 4. ノードの起動
        // Start(key_provider) に合わせる
        node->Start(key_provider);
        
        std::cout << "\n[Main] HCSNode (" << self_id << ") is running on [" 
                  << local_addr << "]:" << media_port << ". Press Ctrl+C to stop.\n";

        // 5. I/Oコンテキストの実行 (イベントループ開始)
        // ノードの動作は、このスレッド内で非同期的に実行される
        std::thread io_thread([&io_context]() {
            boost::system::error_code ec;
            io_context.run(ec);
            if (ec) {
                std::cerr << "[Main Thread] IO context error: " << ec.message() << std::endl;
            }
        });
        
        // --- 簡易シミュレーション (オプション) ---
        // メディアストリームの開始シミュレーションを、io_contextのpostで遅延実行
        io_context.post([&io_context, node_ptr = node]() {
            // 3秒後にStreamEncoderがストリーム送信を開始するのをシミュレート
            std::make_shared<boost::asio::steady_timer>(io_context, std::chrono::seconds(3))->async_wait(
                [node_ptr](const boost::system::error_code& ec) {
                    if (ec) return;
                    std::cout << "\n[Simulation] 3s elapsed. Node is starting to PUBLISH and RECEIVE media.\n";
                    // 実際にはTopologyManagerが最適なピアを選択した後、Encoderが起動する
                    // ここではHCSNodeの内部ロジックがそれを処理すると仮定
                    
                    // TODO: HCSNode::SelectAndStartStream() のロジックが実行される
                }
            );
        });
        // ----------------------------------------

        // スレッドの終了を待機
        io_thread.join();

        // 6. ノードの停止 (I/Oコンテキスト停止後に実行される)
        // ノードが停止されたことを確認
        node->Stop();
        
        std::cout << "[Main] HCSNode stopped successfully.\n";

    } catch (const std::exception& e) {
        std::cerr << "[Main] Fatal Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
