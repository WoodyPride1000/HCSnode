#pragma once

#include "common.h"
#include "TransportAES256.h" // AES256_KEY_SIZE を利用
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <cstring>
#include <string_view> // std::string_view を使用

/**
 * @namespace hcs_net
 * @brief HCSノードのネットワーク関連コンポーネントを格納する名前空間
 */
namespace hcs_net {

// --- PBKDF2 鍵導出の定数 ---
constexpr size_t PBKDF2_ITERATIONS = 310000; ///< PBKDF2の推奨反復回数 (NIST/OWASP推奨値に基づき設定)
constexpr size_t PBKDF2_SALT_SIZE = 16;      ///< ソルトのサイズ (128ビット = 16バイト)
constexpr size_t PBKDF2_OUTPUT_SIZE = AES256_KEY_SIZE; ///< 導出される鍵のサイズ (AES256の鍵長と一致)

/**
 * @brief パスワードベースの鍵導出関数 (PBKDF2) を使用して鍵を提供する具象クラス
 *
 * このクラスは、KeyProvider インターフェースを実装し、
 * 設定されたパスワードとソルトから、安全な暗号化鍵を導出します。
 */
class PBKDF2KeyProvider : public KeyProvider {
public:
    /**
     * @brief コンストラクタ
     *
     * @param password ネットワークセッションのパスワード (平文)
     * @param salt_bytes 使用するソルト。空の場合は内部でランダムに生成されます。
     */
    explicit PBKDF2KeyProvider(std::string_view password, 
                               const std::vector<uint8_t>& salt_bytes = {})
    {
        // 1. ソルトの設定または生成
        if (salt_bytes.empty()) {
            // ソルトが指定されていない場合は、ランダムなソルトを生成
            salt_.resize(PBKDF2_SALT_SIZE);
            if (RAND_bytes(salt_.data(), (int)salt_.size()) != 1) {
                throw std::runtime_error("Failed to generate random salt.");
            }
        } else {
            // 指定されたソルトを使用
            if (salt_bytes.size() != PBKDF2_SALT_SIZE) {
                 throw std::runtime_error("Provided salt size must be 16 bytes.");
            }
            salt_ = salt_bytes;
        }

        // 2. 鍵の導出
        derived_key_.resize(PBKDF2_OUTPUT_SIZE);
        
        // PBKDF2を使用して鍵を導出 (HMAC-SHA256を使用)
        // std::string_view の .data() と .size() を使用して、効率的に処理
        int result = PKCS5_PBKDF2_HMAC(
            password.data(),          // パスワードデータへのポインタ
            (int)password.size(),     // パスワード長
            salt_.data(), 
            (int)salt_.size(),
            (int)PBKDF2_ITERATIONS,
            EVP_sha256(),             // HMACのハッシュアルゴリズム
            (int)derived_key_.size(),
            derived_key_.data()
        );

        if (result != 1) {
            throw std::runtime_error("Failed to derive encryption key using PBKDF2.");
        }
    }

    /**
     * @brief 導出された暗号化鍵を取得する
     * @return 暗号化鍵のバイトベクター
     */
    [[nodiscard]] const std::vector<uint8_t>& GetEncryptionKey() const override {
        return derived_key_;
    }

    /**
     * @brief 鍵導出に使用されたソルトを取得する
     * @return ソルトのバイトベクター
     */
    [[nodiscard]] const std::vector<uint8_t>& GetSalt() const override {
        return salt_;
    }

private:
    std::vector<uint8_t> derived_key_; ///< 導出された最終的な暗号化鍵
    std::vector<uint8_t> salt_;        ///< 鍵導出に使用されたソルト
};

} // namespace hcs_net
