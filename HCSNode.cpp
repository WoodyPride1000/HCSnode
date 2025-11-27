#include <iostream>
#include <memory>
#include <thread>
#include <boost/asio.hpp>
#include "HCSNode.h"

using namespace hcs;

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

        // 1. ノードのインスタンス化
        // メディアポート: 8000 (QUIC/UDP)
        // 制御ポート: 8001 (UDP)
        unsigned short media_port = 8000;
        unsigned short control_port = 8001;
        
        std::shared_ptr<HCSNode> node = std::make_shared<HCSNode>(
            io_context, media_port, control_port
        );

        // 2. ノードの起動
        node->Start();
        
        std::cout << "\n[Main] HCSNode is running. Press Ctrl+C to stop.\n";

        // 3. I/Oコンテキストの実行 (イベントループ開始)
        // ここで制御トランスポートとメディアトランスポートの非同期受信が実行される
        std::thread io_thread([&io_context]() {
            boost::system::error_code ec;
            io_context.run(ec);
            if (ec) {
                std::cerr << "[Main Thread] IO context error: " << ec.message() << std::endl;
            }
        });
        
        // 4. シミュレーション: 制御メッセージの受信をシミュレート
        // 実際には外部ノードから8001ポートにUDPパケットが届く
        io_context.post([&node, &io_context]() {
            std::cout << "\n[Simulation] Simulating incoming control ADVERTISE message in 2 seconds...\n";
            
            // 2秒後に実行されるタイマー
            std::make_shared<boost::asio::steady_timer>(io_context, std::chrono::seconds(2))->async_wait(
                [&node](const boost::system::error_code& ec) {
                    if (ec) return;
                    
                    // ダミーのADVERTISEメッセージデータを作成 (MSG_TYPE_ADVERTISE = 1)
                    std::vector<uint8_t> dummy_adv_message = {MSG_TYPE_ADVERTISE, 0x00, 0x00};
                    hcs_net::Endpoint sender_ep = {"10.0.0.5", 8001};

                    // HCSNodeの受信ハンドラを直接呼び出すことで受信をシミュレート
                    node->HandleControlMessage(dummy_adv_message, sender_ep);
                }
            );
        });

        // スレッドの終了を待機
        io_thread.join();

        // 5. ノードの停止 (I/Oコンテキスト停止後に実行される)
        node->Stop();
        
        std::cout << "[Main] HCSNode stopped successfully.\n";

    } catch (const std::exception& e) {
        std::cerr << "[Main] Fatal Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
