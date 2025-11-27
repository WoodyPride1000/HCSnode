#pragma once

#include "common.h"
#include <openssl/evp.h> // OpenSSLのEVPインターフェースを使用
#include <stdexcept>
#include <iostream>
#include <map>
#include <random> // std::random_device を使用

/**
 * @namespace hcs_net
 * @brief HCSノードのネットワーク関連コンポーネントを格納する名前空間
 */
namespace hcs_net {

// --- AES-256-GCMの定数 ---
constexpr size_t AES256_KEY_SIZE = 32;       ///< 鍵長 (256ビット = 32バイト)
constexpr size_t GCM_IV_SIZE = 12;           ///< GCMの初期化ベクトル(IV)/ノンスサイズ (12バイト)
constexpr size_t GCM_TAG_SIZE = 16;          ///< GCMの認証タグサイズ (16バイト)
constexpr size_t ENCRYPTED_OVERHEAD = GCM_IV_SIZE + GCM_TAG_SIZE; ///< 暗号化後の追加バイト数

/**
 * @brief AES-256-GCMを使用したセキュアなトランスポート層の実装
 *
 * このクラスは、hcs_net::Transportインターフェースを実装し、
 * 送信データの暗号化と受信データの復号化を行います。
 */
class TransportAES256 : public Transport {
public:
    /**
     * @brief コンストラクタ
     * @param key_provider 鍵とソルトを提供する KeyProvider の共有ポインタ
     * @param base_transport 実際のネットワークI/Oを行う基底トランスポート層 (UDP/TCP実装)
     */
    TransportAES256(std::shared_ptr<KeyProvider> key_provider,
                    std::shared_ptr<Transport> base_transport)
        : key_provider_(std::move(key_provider)),
          base_transport_(std::move(base_transport)),
          // IVカウンタの初期値をランダムなシードで設定 (セキュリティの堅牢性向上)
          current_iv_counter_(std::random_device{}()), 
          user_callback_()
    {
        // 基底トランスポートからの受信コールバックをラムダ式で設定
        // std::bindよりも現代的で、キャプチャ[this]によりメンバ関数を呼び出す
        base_transport_->SetReceiveCallback(
            [this](const std::vector<uint8_t>& data, const Endpoint& ep) {
                this->DecryptAndHandle(data, ep);
            });
    }

    /**
     * @brief トランスポート層の起動
     */
    void Start() override {
        base_transport_->Start();
    }

    /**
     * @brief トランスポート層の停止
     */
    void Stop() override {
        base_transport_->Stop();
    }

    /**
     * @brief データを暗号化して基底トランスポートを通じて送信する
     * @param data 暗号化する平文データ
     * @param destination 宛先エンドポイント
     */
    void Send(const std::vector<uint8_t>& data, const Endpoint& destination) override {
        try {
            // 1. データ暗号化
            std::vector<uint8_t> encrypted_packet = Encrypt(data);
            
            // 2. 暗号化済みデータを基底トランスポートで送信
            base_transport_->Send(encrypted_packet, destination);

        } catch (const std::exception& e) {
            std::cerr << "TransportAES256::Send error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief 受信コールバックの設定 (コピー)
     */
    void SetReceiveCallback(ReceiveCallback callback) override {
        user_callback_ = std::move(callback);
    }
    
    /**
     * @brief 受信コールバックの設定 (ムーブ)
     */
    void SetReceiveCallback(ReceiveCallback&& callback) override {
        user_callback_ = std::move(callback);
    }

private:
    std::shared_ptr<KeyProvider> key_provider_;
    std::shared_ptr<Transport> base_transport_;
    ReceiveCallback user_callback_;

    // GCM IV/Nonceの安全性を確保するためのカウンタ
    uint64_t current_iv_counter_;

    /**
     * @brief データをAES-256-GCMで暗号化する
     * @param plaintext 暗号化する平文
     * @return IV, 暗号文, 認証タグを連結したバイトベクター
     */
    std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plaintext) {
        // 鍵の取得
        const auto& key_bytes = key_provider_->GetEncryptionKey();
        if (key_bytes.size() != AES256_KEY_SIZE) {
            throw std::runtime_error("Invalid encryption key size.");
        }
        
        // 1. IV（ノンス）の生成
        uint64_t iv_value = current_iv_counter_++;
        
        // GCM_IV_SIZE (12バイト) のIVを作成
        std::vector<uint8_t> iv(GCM_IV_SIZE);
        // IVの最初の4バイトは固定値（例：0x00, 0x00, 0x00, 0x01）
        iv[0] = 0x00; iv[1] = 0x00; iv[2] = 0x00; iv[3] = 0x01;
        // 残りの8バイトをカウンタ値とする（リトルエンディアンでコピー）
        for (int i = 0; i < 8; ++i) {
            iv[GCM_IV_SIZE - 1 - i] = (uint8_t)(iv_value >> (i * 8));
        }

        // 2. 暗号化処理の準備
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create EVP_CIPHER_CTX.");
        }

        int len = 0;
        int ciphertext_len = 0;
        std::vector<uint8_t> ciphertext(plaintext.size());

        try {
            // 初期化
            if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
                throw std::runtime_error("EVP_EncryptInit_ex failed.");

            // 鍵とIVの設定
            if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_SIZE, NULL))
                throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed.");
            
            if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key_bytes.data(), iv.data()))
                throw std::runtime_error("EVP_EncryptInit_ex (key/iv) failed.");
            
            // 3. 暗号化本体
            if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), (int)plaintext.size()))
                throw std::runtime_error("EVP_EncryptUpdate failed.");
            ciphertext_len = len;

            // 4. 最終処理
            if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len))
                throw std::runtime_error("EVP_EncryptFinal_ex failed.");
            ciphertext_len += len;
            
            // 5. 認証タグの取得
            std::vector<uint8_t> tag(GCM_TAG_SIZE);
            if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, tag.data()))
                throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed.");

            // 6. 結果の結合 (IV | 暗号文 | タグ)
            std::vector<uint8_t> result;
            result.reserve(GCM_IV_SIZE + ciphertext_len + GCM_TAG_SIZE);
            result.insert(result.end(), iv.begin(), iv.end());
            result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);
            result.insert(result.end(), tag.begin(), tag.end());
            
            EVP_CIPHER_CTX_free(ctx);
            return result;

        } catch (...) {
            EVP_CIPHER_CTX_free(ctx);
            throw; // 例外を再スロー
        }
    }
    
    /**
     * @brief データを復号化してユーザーコールバックに渡す
     * @param encrypted_data IV, 暗号文, 認証タグを連結したバイトベクター
     * @param sender 送信元エンドポイント
     */
    void DecryptAndHandle(const std::vector<uint8_t>& encrypted_data, const Endpoint& sender) {
        try {
            std::vector<uint8_t> plaintext = Decrypt(encrypted_data);
            
            // 成功した場合、ユーザー設定のコールバックを呼び出す
            if (user_callback_) {
                user_callback_(plaintext, sender);
            }
        } catch (const std::runtime_error& e) {
            std::cerr << "TransportAES256::Decrypt error: " << e.what() 
                      << " from " << sender.ToString() << std::endl;
        }
    }

    /**
     * @brief AES-256-GCMでデータを復号化する
     * @param encrypted_packet IV, 暗号文, 認証タグを連結したバイトベクター
     * @return 復号化された平文
     * @throw std::runtime_error 認証失敗または復号化エラーの場合
     */
    std::vector<uint8_t> Decrypt(const std::vector<uint8_t>& encrypted_packet) {
        if (encrypted_packet.size() < ENCRYPTED_OVERHEAD) {
            throw std::runtime_error("Encrypted packet is too short.");
        }

        const auto& key_bytes = key_provider_->GetEncryptionKey();
        if (key_bytes.size() != AES256_KEY_SIZE) {
            throw std::runtime_error("Invalid decryption key size.");
        }

        // 1. パケットの分解
        const size_t iv_offset = 0;
        const size_t ciphertext_offset = GCM_IV_SIZE;
        const size_t tag_offset = encrypted_packet.size() - GCM_TAG_SIZE;
        const size_t ciphertext_len = encrypted_packet.size() - ENCRYPTED_OVERHEAD;

        // IV、暗号文、タグへのポインタ
        const uint8_t* iv_ptr = encrypted_packet.data() + iv_offset;
        const uint8_t* ciphertext_ptr = encrypted_packet.data() + ciphertext_offset;
        const uint8_t* tag_ptr = encrypted_packet.data() + tag_offset;

        // 2. 復号化処理の準備
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create EVP_CIPHER_CTX for decryption.");
        }

        int len = 0;
        int plaintext_len = 0;
        std::vector<uint8_t> plaintext(ciphertext_len);
        
        try {
            // 初期化
            if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
                throw std::runtime_error("EVP_DecryptInit_ex failed.");

            // IV長の指定
            if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_SIZE, NULL))
                throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed.");

            // 鍵とIVの設定
            if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key_bytes.data(), iv_ptr))
                throw std::runtime_error("EVP_DecryptInit_ex (key/iv) failed.");

            // 3. 復号化本体
            if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext_ptr, (int)ciphertext_len))
                throw std::runtime_error("EVP_DecryptUpdate failed.");
            plaintext_len = len;

            // 4. 認証タグの設定
            if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_SIZE, (void*)tag_ptr))
                throw std::runtime_error("EVP_CTRL_GCM_SET_TAG failed.");

            // 5. 最終処理と認証
            if (1 != EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len)) {
                // 認証失敗
                throw std::runtime_error("Authentication failed (MAC error). Packet forged or corrupted.");
            }
            plaintext_len += len;
            
            // 復号化に成功した場合、結果を返す
            plaintext.resize(plaintext_len);
            
            EVP_CIPHER_CTX_free(ctx);
            return plaintext;

        } catch (...) {
            EVP_CIPHER_CTX_free(ctx);
            throw; // 例外を再スロー
        }
    }
};

} // namespace hcs_net
