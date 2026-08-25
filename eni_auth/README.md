# eni_auth — サブスクリプションのライセンス確認

easy and nice instruments のネイティブ製品（8-Control / FirstSynth / SuiKinKutsu）
が共通で使うライセンス確認のライブラリです。8-Control 側の Python 版
（`eni_auth.py`）と同じプロトコルを、同じサーバに対して話します。

**このフォルダは製品間でそのままコピーして使えます。** 製品ごとに変えるのは
`eni_config.h` の 2 行（`ENI_PRODUCT` と `ENI_APP_VERSION`）だけです。

## 動きの全体像

```
初回          既定のブラウザでログイン（Device Flow）
              → サーバが Ed25519 で署名したライセンス + 更新用シークレットを返す
              → ローカルに保存

毎回の起動    保存したファイルの署名と期限を確認するだけ。通信ゼロ。
              オフライン（電波の無い現場）でもそのまま立ち上がる

月1回程度     期限が近づいていて、たまたまオンラインなら、裏で1回だけ更新。
              失敗しても無視（期限までは動く）
```

解約を検知してアプリを止める処理は**どこにもありません**。更新されなくなり、
支払い済み期間 + 7 日で自然に期限が切れるだけです。不具合で正規のお客様を
締め出す事故が、構造として起こりません。

## 組み込み方（3 ステップ）

### 1. ビルドに追加する

コンパイル対象に加えるファイル:

```
eni_auth/eni_auth.cpp
eni_auth/eni_json.cpp
eni_auth/eni_http_win.cpp     ← Windows でのみ中身が有効
eni_auth/eni_http_mac.mm      ← macOS でのみ中身が有効（Objective-C++）
eni_auth/eni_http_null.cpp    ← その他のプラットフォーム
eni_auth/vendor/monocypher/monocypher.c
eni_auth/vendor/monocypher/monocypher-ed25519.c
```

`.cpp` の 3 つは中身が丸ごと `#if` で消えるので、全部をビルド対象に入れて
構いません。**`eni_http_mac.mm` だけは macOS 限定にしてください** —
Objective-C++ なので、ObjC++ を持たないコンパイラは `#if` に到達する前に
ファイル自体で失敗します（Linux ビルドで実際に踏みました）。

リンクするもの: Windows は `winhttp.lib`、macOS は `Foundation`
フレームワーク。それ以外に依存はありません。

> ⚠️ **MSVC では `/utf-8` を必ず付けてください。**
> このライブラリの利用者向けメッセージは日本語なので、ソースは UTF-8 です。
> MSVC は既定でマシンの ANSI コードページとして読むため、**日本語ロケール
> (CP932) の PC では構文エラーになります**（FirstSynth の初回 Windows ビルドで
> 実際に発生。3 つの `.vcxproj` の `AdditionalOptions` に `/utf-8` を追加して解決）。
> CI では検出できません（CP1252 のランナーでは何事もなく通るため）。

### 2. プラグイン本体から呼ぶ

```cpp
#include "eni_auth/eni_auth.h"

FirstSynth::FirstSynth(const InstanceInfo& info) : Plugin(info, ...)
{
  const eni::LicenceCheck licence = eni::CheckLicence();

  if (licence.valid)
  {
    // 期限が近ければ裏で更新（別スレッド・失敗は無視）
    if (eni::ShouldRefresh(licence.exp)) eni::RefreshInBackground();
  }
  else
  {
    // 未ライセンス表示に切り替える。音は出さない
  }
}
```

ログイン導線（未ライセンス時に押してもらうボタン）:

```cpp
// 必ずワーカースレッドで。ブラウザ承認を待つ間ずっとブロックします
std::thread([this] {
  eni::Error err;
  const bool ok = eni::RunDeviceFlow(
    [this](const eni::DeviceCode& dc) {
      // 画面に dc.userCode と dc.verificationUri を出す
      // （ブラウザが開かない環境向けの控え。スマホからも入力できます）
    },
    err);

  // ok なら UI を通常表示に、失敗なら err.message を表示
  // err.code == "no_subscription" のときは申込ページ（ENI_SIGNUP_URL）へ誘導
}).detach();
```

### 3. UI の状態を 3 つ用意する

| 状態 | 画面 |
|---|---|
| ライセンスあり | 何も出さない（任意で「残り N 日」。`LicenceCheck::DaysLeft()`） |
| 未ライセンス / 期限切れ | 「ブラウザでログイン」ボタン + 控えの URL とコード |
| 未購読（`no_subscription`） | 上記に加えて申込ページへの導線 |

## 守っていただきたい 3 点

1. **オーディオスレッドからは絶対に呼ばないでください。** ライセンス確認は
   ファイルを読み、更新は通信します。プラグイン生成時（メッセージスレッド）に
   `CheckLicence()` を 1 回呼ぶ形にしてください。
2. **ログインは必ずシステム既定のブラウザで。** WebView2 の中で Auth0 の
   ログイン画面を開いてはいけません（Google が埋め込みブラウザからの
   OAuth を拒否します）。`RunDeviceFlow()` は既定ブラウザを開きます。
3. **`ENI_LICENSE_PUBKEY_HEX` と `ENI_AUTH0_CLIENT_ID` は書き換えないでください。**
   本番の値が入っています。鍵を変えると、お客様の手元にある全ライセンスが
   無効になります。

## SuiKinKutsu に移植するとき

`eni_auth/` フォルダをコピーして、`eni_config.h` を 2 行変えるだけです。

```c
#define ENI_PRODUCT "suikinkutsu"
#define ENI_APP_VERSION "suikinkutsu/1.0.0"
```

ライセンスファイルは全製品で共通（`%APPDATA%\easyandnice\license.json`）なので、
FirstSynth でログイン済みのお客様は SuiKinKutsu では**ログイン不要**です。

## テスト

プラグインのビルドとは独立に、このライブラリだけをテストできます。

```bash
cmake -S eni_auth/tests -B build/eni_auth_tests
cmake --build build/eni_auth_tests
./build/eni_auth_tests/eni_auth_tests
```

101 個の確認項目（署名検証・改竄検出・期限・ライセンスファイル・通信の
エラー処理・更新の判断・JSON）。通信は差し替えたテスト用の実装を通るので、
ネットワークにも本番サーバにも一切触れません。

本番の鍵ペアでの端対端確認も実施済みです（サーバの秘密鍵で署名した
ライセンスを、このライブラリの埋め込み公開鍵が `valid` と判定し、署名の
改竄は `bad_signature`、製品違いは `wrong_product` で拒否することを実測）。

## ファイルの役割

| ファイル | 役割 |
|---|---|
| `eni_auth.h` | 公開 API。ここだけ読めば使えます |
| `eni_config.h` | 製品ごとの設定（**移植時に触るのはここだけ**） |
| `eni_auth.cpp` | 署名検証・ライセンスファイル・Device Flow の本体 |
| `eni_json.h/.cpp` | 小さな JSON の読み書き（外部ライブラリを増やさないため） |
| `eni_http_*.cpp/.mm` | プラットフォームごとの HTTPS |
| `vendor/monocypher/` | Ed25519 検証（BSD-2 / CC0。`VENDOR.md` に出所と版） |
