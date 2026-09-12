# 01. macOS 対応

> **Status**: Draft
> **Related**: `mem_1CexjjDtpj5FvQKP7dqeqq`
> **対象**: `CMakeLists.txt`, `FirstSynth.cpp`, `tests/`

## 方針

単体アプリのビルド・起動・Web UI・発音・認証・プリセット保存を先に確認し、AUv2 / VST3 へ進む。配布用の署名・公証は後続。

既存 CMake を正本として Xcode プロジェクトを生成する。waki さんの変更版 iPlug2 を `IPLUG2_DIR` で指定する。未指定なら隣接ディレクトリを参照する。`FIRSTSYNTH_FORMATS` で対象形式を選択し、既定は単体アプリ。Windows の Visual Studio プロジェクトは維持する。

## 構成・保存形式

既存の DSP と Web UI を共用し、eni_auth の macOS 通信と Foundation をリンクする。Web UI の JS と SVG をアプリに同梱する。

プリセットと自動保存のデータ形式は既存の状態チャンクを維持する。保存先は iPlug2 の `INIPath` 配下とし、OS に応じた区切り文字を使う。

## 検証

ビルド、認証の既存テスト、保存パスの回帰テストを自動確認する。実際のウィンドウ表示・音声・ブラウザ認証・DAW の読込は実機確認として別途記録する。

## やってはいけない

- 旧スキームは存在しない `FirstSynth-macOS.xcodeproj` を参照する。生成したプロジェクトの手編集で対処しない。
- POSIX のパスに Windows のバックスラッシュを付加しない。
- ビルド成功を発音や認証成功として扱わない。

## Status log

- 2026-09-12: 方針合意。旧スキームの参照切れを再現。CMake の認証ソースと Web リソースを整備開始。
- 2026-09-12: Synth 依存を追加。保存パスは修正前の失敗と修正後の成功を確認。認証テストも成功。本体は手元の iPlug2 に `GetScreenScale` / `SetAttackShape` が無くコンパイル停止。後者の仕様は progress.md にあるが、変更ソースの所在をユーザーに照会中。
- 2026-09-12: ユーザーの案内から `waki-loveburger/iPlug2` に必要な API を確認。変更版の commit を guide に記録し、作業用 clone でビルドを再開。
- 2026-09-12: 変更版 iPlug2 で Debug / arm64 単体アプリのビルド成功。起動要求を実行。音声・認証・UI 操作の実機確認待ち。
- 2026-09-12: ユーザーがログイン済み・設定変更後の発音を確認。MIDI 初期値は off。出荷プリセット37個を同梱し初回配置する処理、空一覧の base64 エラー対策を追加。CoreAudio のデバイス名は RtAudio.cpp の UNICODE 定義で UTF-8 を選び、表示の文字化けに対処。
