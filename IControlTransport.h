#pragma once
#include "TransportBase.h" // Endpoint, SendCallback

namespace hcs_net {

/**
 * @brief 制御メッセージのためのトランスポート抽象インターフェース。
 * メディアデータとは異なり、セキュリティ機能は簡略化されるか、よりシンプルな実装に依存する可能性がある。
 */
class IControlTransport {
public:
    using RecvHandler = IMediaTransport::RecvHandler;
    using SendCallback = IMediaTransport::SendCallback;

    virtual ~IControlTransport() = default;

    /**
     * @brief 受信を開始し、メッセージハンドラを登録する。
     */
    virtual void StartReceive(RecvHandler handler) = 0;

    /**
     * @brief 制御メッセージを非同期的に送信する。
     */
    virtual void AsyncSendTo(const std::vector<uint8_t>& message,
                            const Endpoint& dest,
                            SendCallback on_sent = nullptr) = 0;

    /**
     * @brief トランスポート層を停止する。
     */
    virtual void Stop() = 0;
};

} // namespace hcs_net
