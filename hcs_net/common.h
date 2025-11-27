#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional> // std::function を使用

/**
 * @namespace hcs_net
 * @brief HCSノードのネットワーク関連コンポーネントを格納する名前空間
 */
namespace hcs_net {

/**
 * @brief ネットワークエンドポイント (IPアドレスとポート) を表す構造体
 */
struct Endpoint {
    std::string address; ///< IPアドレス (IPv4またはIPv6)
    uint16_t port;       ///< UDP/TCPポート番号

    /**
     * @brief コンストラクタ
     */
    Endpoint(const std::string& addr, uint16_t p) : address(addr), port(p) {}
    
    // --- 比較演算子の追加 (ユーザー要求による) ---

    /**
     * @brief 等価比較演算子
     */
    bool operator==(const Endpoint& other) const {
        return address == other.address && port == other.port;
    }

    /**
     * @brief 非等価比較演算子
     */
    bool operator!=(const Endpoint& other) const {
        return !(*this == other);
    }

    /**
     * @brief 順序比較演算子 (std::map/std::setのキーとして使用可能にするため)
     */
    bool operator<(const Endpoint& other) const {
        // まずアドレスで比較し、アドレスが同じ場合はポートで比較する
        return address < other.address || (address == other.address && port < other.port);
    }
};

/**
 * @brief マスター鍵とソルトを管理するための抽象インターフェース (前方宣言)
 *
 * 実際の認証や鍵導出はこのインターフェースの実装に依存する。
 * (例: TransportAES256.hで具象クラスとして定義される)
 */
class KeyProvider {
public:
    virtual ~KeyProvider() = default;

    /**
     * @brief データの暗号化に使用する鍵を取得する
     * @return 鍵バイトのベクター
     */
    virtual const std::vector<uint8_t>& GetEncryptionKey() const = 0;

    /**
     * @brief 鍵導出に使用するソルトを取得する
     * @return ソルトバイトのベクター
     */
    virtual const std::vector<uint8_t>& GetSalt() const = 0;
};

// --- トランスポート層の抽象化 ---

/**
 * @brief すべてのトランスポート層コンポーネントの抽象ベースクラス
 */
class Transport {
public:
    virtual ~Transport() = default;

    /**
     * @brief トランスポート層の起動
     */
    virtual void Start() = 0;

    /**
     * @brief トランスポート層の停止
     */
    virtual void Stop() = 0;

    /**
     * @brief データの送信
     * @param data 送信するバイトデータ (暗号化済みを想定)
     * @param destination 宛先エンドポイント
     */
    virtual void Send(const std::vector<uint8_t>& data, const Endpoint& destination) = 0;

    /**
     * @brief 受信コールバックの設定
     *
     * 受信コールバックは、トランスポート層で復号化が完了した後に呼び出される。
     * @param callback 受信データと送信元エンドポイントを受け取る関数
     */
    using ReceiveCallback = std::function<void(const std::vector<uint8_t>&, const Endpoint&)>;
    virtual void SetReceiveCallback(ReceiveCallback callback) = 0;
};

} // namespace hcs_net
