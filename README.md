Hybrid Communication System

Hybrid Communication System (HCS) Secure Network 通信プロトコル設計書

1. 序論

1.1. 目的

本設計書は、Hybrid Communication System (HCS) プロジェクトで使用されるセキュアなP2P（Peer-to-Peer）ネットワーク通信レイヤーの実装仕様を定義するものです。非同期I/OとUDP（User Datagram Protocol）を基盤とし、PBKDF2による共通鍵導出とAES-256-GCMによる暗号化を組み込むことで、信頼性の低いUDP上で機密性と完全性の高いデータ伝送を実現することを目的とします。

1.2. 解決する課題

標準的なUDP通信は、データグラムの喪失、順序の入れ替わり、および第三者による傍受・改ざんに対するセキュリティ機能を提供しません。本プロトコルは、これに対し、データグラム単位の暗号化と認証（GCMタグ）を適用することで、UDPの低遅延という利点を維持しつつ、安全性を確保します。

2. アーキテクチャとレイヤー構造

本ライブラリは、トランスポート機能を抽象化し、暗号化を透過的に処理するレイヤー構造を採用しています。 

レイヤー

クラス/モジュール

役割

アプリケーション層

main.cpp / ユーザーコード

暗号化を意識せず、バイトベクターを送受信する。

セキュリティ層

TransportAES256

データを暗号化/復号化し、認証タグを検証する。

基底トランスポート層

UdpTransport

ネットワークI/O（UDPソケット操作）を非同期で実行する。

鍵管理層

KeyProvider / PBKDF2KeyProvider

暗号化/復号化に必要な鍵とソルトを導出・提供する。

3. モジュール設計

3.1. I/O抽象化: Transport インターフェース

全てのトランスポート実装の基盤となる純粋仮想インターフェースです。

メソッド

説明

void Send(const std::vector<uint8_t>& data, const Endpoint& destination)

指定された宛先へデータを送信します。

void Start()

非同期受信処理を開始します。

void Stop()

トランスポートを停止し、リソースを解放します。

void SetReceiveCallback(ReceiveCallback callback)

データ受信時に呼び出されるコールバックを設定します。

3.2. 鍵抽象化: KeyProvider インターフェース

鍵導出方法に依存しない形で、暗号化に必要な共通鍵とソルトを提供するインターフェースです。

メソッド

説明

const std::vector<uint8_t>& GetEncryptionKey() const

共通鍵（256ビット = 32バイト）を提供します。

const std::vector<uint8_t>& GetSalt() const

鍵導出に使用されたソルトを提供します。

3.3. 基底実装: UdpTransport

Boost.Asioを用いて、指定されたポートでUDPソケットを開き、非同期で送受信を実行します。

機能: UDPデータグラムの直接的な送受信。

特徴: 受信したデータグラムをそのままバイトベクターとしてコールバックに渡します。暗号化/復号化の知識は持ちません。

3.4. 鍵導出実装: PBKDF2KeyProvider

パスワードベースの鍵導出関数PBKDF2（Password-Based Key Derivation Function 2）を用いて、共有パスワードから暗号化鍵を生成します。

アルゴリズム: PBKDF2 with SHA256

鍵長: 256ビット (32バイト)

ソルト:

初回実行時にランダムに生成され、永続化されます。

PBKDF2の仕様上、通信相手も同じ鍵を導出するためには、ソルトを共有する必要があります。このクラスは、ノード間の鍵導出の一貫性を保つために、導出したソルトも提供します。

イテレーション数: 2048回 (セキュリティと性能のバランスを考慮した初期設定)

3.5. セキュリティ実装: TransportAES256

UdpTransportをラップし、データの暗号化、復号化、および認証を行うコアセキュリティモジュールです。

使用する暗号: AES-256-GCM (Galois/Counter Mode)

機能:

送信時: ペイロードをAES-256-GCMで暗号化し、認証タグを付加してから、基底トランスポート（UdpTransport）に渡します。

受信時: 受信データから認証タグとIVを抽出し、復号化を行います。GCMタグ検証に失敗した場合、パケットは破棄され、コールバックは呼び出されません（認証失敗によるデータ改ざんの防止）。

4. 暗号化プロトコル詳細 (データグラムフォーマット)

UDPデータグラムとして送信されるデータの構造は以下の通りです。

フィールド

サイズ (バイト)

説明

IV (Initialization Vector)

12

GCMモードで使用されるランダムな初期化ベクトル。データグラムごとにユニークです。

Ciphertext

可変長

平文データがAES-256-GCMで暗号化された結果。

Authentication Tag

16

GCMによって生成された認証タグ（Integrity Check Value: ICV）。暗号文の完全性を保証します。

データグラムの合計サイズ = 12 (IV) + Ciphertext サイズ + 16 (Tag)

4.1. 処理フロー

処理

送信側 (TransportAES256::Send)

受信側 (TransportAES256::ReceiveCallback)

暗号化

1. 鍵 (KeyProvider::GetEncryptionKey()) を取得。



2. 12バイトのランダムなIVを生成。



3. AES-256-GCMで平文を暗号化（CiphertextとTagを生成）。



4. [IV

Ciphertext

復号化

-

3. 受信した鍵とIV、Tagを使用して復号化を試行。

認証

-

4. GCM復号化時にTagの検証が同時に行われます。



5. 成功時: 復号化された平文をユーザーコールバックに渡す。



6. 失敗時: パケットを静かに破棄し、エラーログを出力する（サイレントドロップ）。

5. 依存関係

本ライブラリは、以下の外部ライブラリに依存します。

Boost.Asio: 非同期ネットワークI/O（UDPソケット操作）に使用。

OpenSSL: AES-256-GCM暗号化/復号化、PBKDF2鍵導出、およびセキュアな乱数生成（IV生成用）に使用。
