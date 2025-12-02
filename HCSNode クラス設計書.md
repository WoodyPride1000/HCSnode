HCSNode クラス設計書

1. 概要 (Overview)

HCSNodeクラスは、Hybrid Communication System (HCS) のセキュアなP2P通信機能を提供する最上位の公開APIです。本クラスは、トランスポート層（UDP）、鍵管理層（PBKDF2）、およびセキュリティ層（AES-256-GCM）の各コンポーネントを統合し、ユーザーアプリケーションに対してシンプルで安全なデータグラム通信インターフェースを提供します。

目的

HCSの全通信機能を単一のオブジェクトにカプセル化する。

基盤となる複雑な暗号化やネットワークI/Oの詳細をユーザーから隠蔽する。

安全かつ非同期なデータグラム送受信機能を提供する。

2. クラス構造 (Class Structure)

クラス名

HCSNode

依存関係 (Dependencies)

HCSNodeは以下の内部コンポーネントに依存し、そのライフサイクルを管理します。

PBKDF2KeyProvider: 鍵導出のため。

UdpTransport: 生のUDP通信のため。

TransportAES256: 暗号化/復号化のため。

boost::asio::io_context: 非同期I/O処理のため。

3. インターフェース定義 (Public API)

3.1. コンストラクタ

HCSNodeのインスタンス化には、ノードを識別し、共通鍵を導出するための情報が必要です。

/**
 * @brief HCSNodeコンストラクタ
 * * @param bind_port ノードがリッスンするローカルポート
 * @param shared_secret 相手ノードと共有するパスフレーズ
 * @param salt 鍵導出に使用するソルト
 */
HCSNode(uint16_t bind_port, const std::string& shared_secret, const std::string& salt);


3.2. 制御メソッド

メソッド

戻り値

説明

void Start()

void

非同期I/O処理を開始します。ブロッキングコールを避けるため、通常は別スレッドで実行されます。

void Stop()

void

非同期I/O処理と内部スレッドを停止し、リソースをクリーンアップします。

void Run()

void

io_contextの処理ループを実行します。Start()は内部コンポーネントを準備し、このメソッドがI/O処理を開始します。

3.3. データ送信メソッド

送信データは、TransportAES256によって自動的に暗号化されます。

/**
 * @brief 指定したエンドポイントへデータを送信します。
 * * @param remote_endpoint 送信先ノードのIPとポート
 * @param data 送信するバイトデータ
 * @param callback 送信完了時に呼び出されるコールバック
 */
void Send(const Endpoint& remote_endpoint, const std::vector<uint8_t>& data, 
          std::function<void(const std::error_code&)> callback);


3.4. 受信ハンドラー設定

受信したデータグラムを処理するためのコールバックを設定します。データは既に復号化され、認証が完了しています。

/**
 * @brief 受信データ処理ハンドラーを設定します。
 * * @param handler 受信データと送信元エンドポイントを受け取るコールバック
 */
using ReceiveHandler = std::function<void(const Endpoint& sender, const std::vector<uint8_t>& data)>;
void SetReceiveHandler(ReceiveHandler handler);


4. 内部実装の詳細 (Implementation Details)

4.1. 内部構造

HCSNodeは以下のプライベートメンバーを保持します。

private:
    // Boost.Asio I/O処理コンテキスト
    boost::asio::io_context io_context_;

    // 鍵管理、トランスポート、セキュリティ層
    std::unique_ptr<PBKDF2KeyProvider> key_provider_;
    std::unique_ptr<UdpTransport> raw_transport_;
    std::unique_ptr<TransportAES256> secure_transport_;

    // 受信ハンドラー
    ReceiveHandler user_receive_handler_;


4.2. 処理フロー

コンストラクタ:

io_context_を初期化。

key_provider_を共有秘密鍵とソルトで初期化。

raw_transport_をバインドポートで初期化。

secure_transport_をraw_transport_とkey_provider_で初期化。

secure_transport_の内部受信ハンドラーをHCSNode::handle_receiveに設定。

Start() / Run():

secure_transport_に対してリスニング開始を指示（内部でUdpTransportの非同期受信ループを開始）。

io_context_.run()を呼び出し、I/Oイベント処理を開始。

Send():

ユーザーデータをsecure_transport_->Send()に渡します。

TransportAES256が鍵を使用してデータを暗号化し、UdpTransport経由で送信します。

受信処理 (handle_receive):

TransportAES256が暗号化されたデータグラムを受信・復号・認証チェックを行います。

認証成功時: 復号化されたデータと送信元エンドポイントがHCSNode::handle_receiveに渡されます。

HCSNode::handle_receiveは、ユーザーが設定した**user_receive_handler_**を呼び出します。

5. エラー処理とセキュリティ (Error Handling & Security)

5.1. 暗号化/復号化エラー

TransportAES256層でGCMタグ検証が失敗した場合（認証失敗）、そのデータグラムは静かに破棄され、user_receive_handler_は呼び出されません。これにより、偽装されたり改ざんされたデータがアプリケーション層に到達するのを防ぎます。

5.2. ネットワークI/Oエラー

Send()時のエラー（例：経路なし）は、std::error_codeを介してユーザー提供のコールバックに報告されます。

非同期受信時のエラーは、ログに記録されますが、通常は受信ループを継続します（致命的でない場合）。

5.3. 鍵管理

HCSNodeは、shared_secretとsaltから導出された鍵を内部のKeyProviderに保持します。鍵は外部から取得不可能であり、機密性を維持します。
