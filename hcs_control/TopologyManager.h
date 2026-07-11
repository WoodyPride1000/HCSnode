namespace hcs_control {

/**
 * @brief ノードの品質と経路を評価するためのメトリクス構造体。
 */
struct NodeMetrics {
    int hop_count = 0;              // 親ノードまでのホップ数 (少ない方が良い)
    int bandwidth_score = 0;        // 帯域幅の品質スコア (高い方が良い)
    int stability_score = 0;        // 安定性/稼働時間のスコア (高い方が良い)
    long long control_rtt_ms = 9999; // 制御プレーンRTT (ADVERTISE/HEARTBEATの往復)
    long long app_rtt_ms = 9999;     // アプリケーションプレーンRTT (メディアストリームの往復)
};

/**
 * @brief ピアディスカバリおよびステータス交換のためのADVERTISEメッセージ構造体。
 */
struct AdvertiseMessage {
    std::string ip;
    NodeMetrics metrics;
    std::set<std::string> groups;
};

class TopologyManager {
    // ...(Start/HandleAdvertise/HandleHeartbeat/SelectBestParent は変更なし)...

private:
    /**
     * @brief 複数のメトリクスに基づき、ノードの総合評価スコアを計算する。
     *
     * Control RTT と App RTT を分離して評価する。QoS環境下でメディアストリームのみが
     * 輻輳している場合に、制御チャネルの健全性まで誤って劣化判定しないための分離。
     */
    double ComputeNodeScore(const NodeMetrics& metrics) const {
        double score = 1000.0
                       - metrics.hop_count * 10.0
                       + metrics.bandwidth_score * 5.0
                       + metrics.stability_score * 2.0
                       - metrics.control_rtt_ms * 0.1
                       - metrics.app_rtt_ms * 0.05;

        // 制御RTTの著しい劣化はトポロジーの健全性そのものに関わるため重いペナルティ
        if (metrics.control_rtt_ms > 1000) score -= 500.0;
        // アプリRTTの劣化はメディア品質の問題であり、制御ほど重くは扱わない
        if (metrics.app_rtt_ms > 1000) score -= 100.0;

        return score;
    }
};

} // namespace hcs_control
