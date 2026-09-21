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

- 2026-09-22: `scripts/dist-mac.sh` を MediSynth の現行に揃えた（引数のガード、出荷 bundle への verify、公証拒否時の理由保存、README 同梱、`.partial` DMG）。`scripts/verify-auv3.py` / `verify-vst3.py` を移植（FirstSynth の appex は `files.user-selected` を持たないので要求しない）。AUv3 表示テストの計測をポーリングにし、両順序 7/7。
- 2026-09-22: MediSynth lane の指摘で、macOS の plist に書かれた版（文字列・AUv3 の整数版・最低 OS）が `config.h` から取り残される穴を塞いだ。configure で検査して止める。`IPLUG_DEPLOY_PLUGINS` は CMakeLists で OFF に固定（iPlug2 の既定 ON は `~/Applications` の app を build tree への symlink に置き換える）。検証中に Xcode 27 の SDK が deployment target 11.3 を拒否することが分かり、最低対応 macOS を 12.0 に上げた（mako 決定、MediSynth と共通）。
- 2026-09-21: 斎藤さんの MacBook で FirstSynth が Gatekeeper に拒否された。共有ドライブに旧ビルドが残っていたことと、手順書どおりに `ditto -c -k --keepParent` で作った配布 ZIP が Finder 以外の展開で AppleDouble `._*` を bundle 内に残し、署名の封印を破ることが原因と見ている。MediSynth の `scripts/dist-mac.sh` を移植し、配布を署名・公証・staple 済みの DMG に統一した（mako 決定、MediSynth と共通）。梱包部分は関数に分け、事故の回帰テスト `tests/dist_mac_package_test.sh` を追加。手順は `docs/guide/04-macos-distribution.md`。creo `mem_1CfGcTACNPbopUr5rq8TvW`。同日、配布 ZIP の作成もやめて DMG だけにした（mako。MediSynth は GitHub Releases 用に ZIP を残す）。
- 2026-09-12: Mac VST3 を追加検証。SDK 3.8.1 を固定し、バンドル識別子を実装側の `com.EASYANDNICE.vst3.FirstSynth` に揃える。旧 AcmeInc の識別子では Web UI のリソース探索に失敗する。Mac VST3 と AUv3 は共通の `EnablePluginFit` / `FitPluginToSize` で全ページを縮小し、VST3 は `ConstrainEditorResize` で縦横比を返す。VST3 の初期サイズは 1110×606。Windows の UI 処理・VST3 クラス ID・パラメータ ID は維持する。実機確認先は Ableton Live。

- 2026-09-12: 方針合意。旧スキームの参照切れを再現。CMake の認証ソースと Web リソースを整備開始。
- 2026-09-12: Synth 依存を追加。保存パスは修正前の失敗と修正後の成功を確認。認証テストも成功。本体は手元の iPlug2 に `GetScreenScale` / `SetAttackShape` が無くコンパイル停止。後者の仕様は progress.md にあるが、変更ソースの所在をユーザーに照会中。
- 2026-09-12: ユーザーの案内から `waki-loveburger/iPlug2` に必要な API を確認。変更版の commit を guide に記録し、作業用 clone でビルドを再開。
- 2026-09-12: 変更版 iPlug2 で Debug / arm64 単体アプリのビルド成功。起動要求を実行。音声・認証・UI 操作の実機確認待ち。
- 2026-09-12: ユーザーがログイン済み・設定変更後の発音を確認。MIDI 初期値は off。出荷プリセット37個を同梱し初回配置する処理、空一覧の base64 エラー対策を追加。CoreAudio のデバイス名は RtAudio.cpp の UNICODE 定義で UTF-8 を選び、表示の文字化けに対処。
