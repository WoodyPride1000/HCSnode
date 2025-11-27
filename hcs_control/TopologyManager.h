#pragma once

#include <map>
#include <set>
#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <iostream>

namespace hcs_control {

/**
 * @brief ノードの品質と経路を評価するためのメトリクス構造体。
 */
struct NodeMetrics {
    int hop_count = 0;        // 親ノードまでのホップ数 (少ない方が良い)
    int bandwidth_score = 0;  // 帯域幅の品質スコア (高い方が良い)
    int stability_score = 0;  // 安定性/稼働時間のスコア (高い方が良い)
    long long rtt_ms = 9999;  // ラウンドトリップタイム (ms) (低い方が良い)
};

/**
 * @brief 近隣ノードの現在の状態を保持する構造体。
 */
struct PeerState {
    NodeMetrics metrics;
    std::string ip_address;
    std::chrono::steady_clock::time_point last_advertise_time; // 最終受信時刻
    double score = -1.0;                                     // 計算されたノードスコア
    bool is_parent = false;                                  // 現在の親ノードであるか
    std::set<std::string> groups;                             // 所属グループID
};

/**
 * @brief ピアディスカバリおよびステータス交換のためのADVERTISEメッセージ構造体。
 */
struct AdvertiseMessage {
    std::string ip;
    NodeMetrics metrics;
    std::set<std::string> groups;
};

/**
 * @brief トポロジーマネージャ本体
 * ノード間のピアディスカバリ、親ノード選定、およびヘルスチェックのロジックを管理する。
 */
class TopologyManager {
public:
    TopologyManager() = default;

    /**
     * @brief マネージャを起動し、定期的な処理（タイマー）を開始する。
     */
    void Start() {
        // TODO: 定期的なADVERTISE送信やHEARTBEATチェックタイマーを設定
        std::cout << "[TopologyManager] Started. Failover timeout set to " 
                << failover_timeout_sec_ << "s.\n";
    }

    /**
     * @brief ADVERTISEメッセージを受信し、近隣ノードの状態を更新する。
     * @param msg 受信したADVERTISEメッセージ
     */
    void HandleAdvertise(const AdvertiseMessage& msg) {
        auto now = std::chrono::steady_clock::now();
        auto& peer = neighbor_nodes_[msg.ip];
        peer.ip_address = msg.ip;
        peer.metrics = msg.metrics;
        peer.last_advertise_time = now;
        peer.groups = msg.groups;

        // グループごとにスコア計算し、最良ノードを更新
        for (const auto& gid : msg.groups) {
            double score = ComputeNodeScore(msg.metrics);
            
            // スコアが更新された場合のみ記録
            if (score > best_scores_[gid].score) {
                best_scores_[gid].score = score;
                best_scores_[gid].parent_ip = msg.ip;
            }
        }
    }

    /**
     * @brief HEARTBEAT（生存確認）メッセージを受信し、最終受信時刻を更新する。
     * @param ip 送信元IPアドレス
     * @param group_id グループID (現在はIPで管理)
     */
    void HandleHeartbeat(const std::string& ip, const std::string& group_id) {
        auto it = neighbor_nodes_.find(ip);
        if (it != neighbor_nodes_.end()) {
            it->second.last_advertise_time = std::chrono::steady_clock::now();
        }
    }

    /**
     * @brief 指定されたグループIDの現在の最良親ノードのIPアドレスを返す。
     * @param group_id グループID
     * @return 最良親ノードのIPアドレス、見つからない場合は空文字列
     */
    std::string SelectBestParent(const std::string& group_id) {
        auto it = best_scores_.find(group_id);
        if (it != best_scores_.end()) return it->second.parent_ip;
        return "";
    }

    /**
     * @brief 現在の親ノードの生存状態をチェックし、タイムアウトした場合は選定をリセットする。
     * @param group_id チェック対象のグループID
     */
    void CheckParentHealth(const std::string& group_id) {
        auto parent_ip = SelectBestParent(group_id);
        if (parent_ip.empty()) return;

        auto it = neighbor_nodes_.find(parent_ip);
        if (it == neighbor_nodes_.end()) return;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_advertise_time).count();

        if (elapsed > failover_timeout_sec_) {
            std::cout << "[TopologyManager] Parent " << parent_ip
                      << " for group " << group_id << " is considered down (Timeout: "
                      << elapsed << "s).\n";
            // 親フラグのリセットとスコアの削除
            it->second.is_parent = false;
            best_scores_.erase(group_id);
            
            // TODO: HCSNodeに対して親変更の必要性を通知するコールバックを呼び出す
        }
    }

private:
    /**
     * @brief グループごとの最良ノードを追跡するための構造体。
     */
    struct BestScore {
        double score = -1.0;
        std::string parent_ip;
    };

    std::map<std::string, PeerState> neighbor_nodes_; // IPアドレス -> PeerState
    std::map<std::string, BestScore> best_scores_;    // GroupID -> BestScore

    int failover_timeout_sec_ = 5; // HEARTBEAT タイムアウト時間 (秒)

    /**
     * @brief 複数のメトリクスに基づき、ノードの総合評価スコアを計算する。
     * @param metrics 評価対象のノードメトリクス
     * @return 計算されたスコア (高いほど優秀)
     */
    double ComputeNodeScore(const NodeMetrics& metrics) const {
        // スコア計算式 (調整可能):
        // (低くあるべき) hop_count は減点、rtt_ms も減点
        // (高くあるべき) bandwidth_score, stability_score は加点
        double score = 1000.0 // ベーススコア
                       - metrics.hop_count * 10.0
                       + metrics.bandwidth_score * 5.0
                       + metrics.stability_score * 2.0
                       - metrics.rtt_ms * 0.1;
        return score;
    }
};

} // namespace hcs_control
