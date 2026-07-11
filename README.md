# Hybrid Communication System (HCS)  
セキュアP2Pネットワーク通信プロトコル設計書

**Rev 3.0**（アーキテクチャ確定・スケーラビリティ制約反映版）

## 0. 改訂履歴と本改訂の位置づけ

【重大・最優先】  
v2.0 の Pull型 IMediaTransport 確定は誤りだった。実装（UdpTransport, TransportAES256, PBKDF2KeyProvider）はすべて `common.h` の **Push型 Transport** インターフェースに基づいて書かれており、Pull型は設計書上の提案のみで実装に一度も採用されていなかった。本書で **Push型を正式仕様** として再確定する。

## 1. 序論

### 1.1. 目的

本設計書は、HCSプロジェクトで使用されるセキュアなP2Pネットワーク通信レイヤーの実装仕様を定義する。非同期I/Oを基盤とし、**メディアデータ用チャネル**と**制御（トポロジー管理）用チャネル**を分離した二重チャネル構成により、機密性・完全性の高いデータ伝送と、低遅延な制御メッセージ交換を両立する。

### 1.2. 解決する課題

標準的なUDP通信は、データグラムの喪失、順序の入れ替わり、および第三者による傍受・改ざんに対する保護を提供しない。本プロトコルはメディアチャネルに対し暗号化と認証を適用することでこれに対処する。

加えて、想定端末数（最大1,000台規模）と物理層帯域（64kbps以下）の制約から、制御メッセージの配送方式そのものがスケーラビリティのボトルネックになることが判明しており、本改訂ではこれを**正式な設計制約**として扱う（4章参照）。

## 2. アーキテクチャとレイヤー構造

**【確定】** トランスポート層は **Push型** で統一する。呼び出し側が受信コールバックを事前登録し（`SetReceiveCallback`）、データ到着時にトランスポート側から能動的に呼び出す方式。

QUIC/TLS1.3への移行は将来課題とし、現行は自前の **AES-256-GCM実装**（`TransportAES256` によるデコレータパターン）を正式仕様とする。

**【未解決】**  
`QuicNgTcp2Transport`（Pull型 `IMediaTransport` の実装として書かれたクラス）の扱いが未確定。中身はQUICを一切使用しておらずAES-GCM+生UDPが実体である一方、`common.h`のPush型Transportとはインターフェースが非互換。

**方針候補**：
- (a) 廃棄し、`UdpTransport+TransportAES256`をメディアチャネルの唯一の実装とする
- (b) Push型Transportインターフェースに合わせて全面書き直しする

## 3. モジュール設計

### 3.1. Endpoint / Transport インターフェース（common.h・正式版）

```cpp
namespace hcs_net {

struct Endpoint {
    std::string address;   // IPアドレス（IPv4/IPv6）
    uint16_t port;
    std::string ToString() const { return address + ":" + std::to_string(port); }
    bool operator==(const Endpoint&) const;
    bool operator<(const Endpoint&) const;  // map/setキーとして使用可能
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Send(const std::vector<uint8_t>& data, const Endpoint& destination) = 0;

    using ReceiveCallback = std::function<void(const std::vector<uint8_t>&, const Endpoint&)>;
    virtual void SetReceiveCallback(ReceiveCallback callback) = 0;  // 値渡し1本に統一
};

} // namespace hcs_net

注：SetReceiveCallback は当初コピー版／ムーブ版の2オーバーロードで実装されていたが、値渡し1本（pass-by-value-then-move） に統一する。3.2. KeyProvider インターフェース【確定】cpp

class KeyProvider {
public:
    virtual ~KeyProvider() = default;
    [[nodiscard]] virtual const std::vector<uint8_t>& GetEncryptionKey() const = 0;
    [[nodiscard]] virtual const std::vector<uint8_t>& GetSalt() const = 0;
};

3.3. PBKDF2KeyProvider（実装）共有パスフレーズとソルトから PBKDF2-HMAC-SHA256 により鍵を導出する。【未解決】 導出された鍵・ソルトはプロセス終了/デストラクト時にメモリ上でゼロクリアされていない（OPENSSL_cleanse等の未使用）。デストラクタでの明示的ゼロ化を追加すべき。3.4. TransportAES256（暗号化デコレータ）【重大・最優先】
GCM Nonce（IV）の生成方式に重大な脆弱性がある。
【重大・最優先】 Send() 呼び出し時に use-after-free の危険がある。3.5. UdpTransport（生トランスポート実装）Boost.Asio による非同期UDP送受信を行う。3.6. TopologyManager（トポロジー管理）複数の**【未解決】**事項あり（ノード識別モデル、親ノード再選定、プルーニング、マルチスレッド排他制御など）。4. スケーラビリティ制約（新設）結論：フラット・マルチキャスト方式は約127台が上限。1,000台規模には階層化トポロジーが必須。4.2. 64kbps制約下での許容ノード数試算text

受信負荷(bps) = (N - 1) × 158bytes × 8bit / 5sec

N=1000 → 約252.3 kbps（64kbps比 約394%超過）
安全マージン50%時 → 上限 N ≈ 127台

4.3. 階層化設計（必須要件）100台程度を上限とする複数グループに分割
グループ間は親/ゲートウェイノード経由のユニキャストのみ

5. 制御メッセージプロトコル（暫定）複数の未解決事項あり（ワイヤーフォーマット、境界チェックなど）。6. セキュリティ6.1. メディアチャネルAES-256-GCM（Nonce再利用問題は最優先対応事項）6.2. 制御チャネル現行 無暗号・無認証（要検討）8. 未解決事項 一覧（サマリ）重大GCM Nonce再利用の脆弱性への対策
Send()時のuse-after-free修正
制御チャネルの暗号化要否
QuicNgTcp2Transport の扱い

（その他未解決事項多数）

