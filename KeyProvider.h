#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace hcs_control {

/**
 * @brief マスターキーとソルトを提供する抽象クラス。
 * 鍵交換プロトコルや外部の鍵管理サービスに依存する。
 * 現在はダミーの実装を提供する。
 */
class KeyProvider {
public:
    virtual ~KeyProvider() = default;

    /**
     * @brief 鍵導出のためのマスターキーを取得する。
     */
    virtual std::vector<uint8_t> GetMasterKey() const {
        // ダミー実装: 32バイトの固定キー
        return std::vector<uint8_t>(32, 0xAA);
    }

    /**
     * @brief 鍵導出のためのソルトを取得する。
     */
    virtual std::vector<uint8_t> GetMasterSalt() const {
        // ダミー実装: 8バイトの固定ソルト
        return std::vector<uint8_t>(8, 0xBB);
    }
};

} // namespace hcs_control
