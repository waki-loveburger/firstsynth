# 01. macOS ビルド

## 前提

Xcode、CMake、waki さんの変更版 iPlug2 が必要。iPlug2 の場所は `-DIPLUG2_DIR=/path/to/iPlug2` で指定する。

依存: `waki-loveburger/iPlug2`、確認した commit は `513182eb1b9f83fe20b453022d75d678f704de1a`。アクセス権が必要。一般の upstream には、本体が必要とする `ADSREnvelope::SetAttackShape()` と WebView の `GetScreenScale()` がない。

```sh
gh repo clone waki-loveburger/iPlug2 private/iPlug2
git -C private/iPlug2 checkout 513182eb1b9f83fe20b453022d75d678f704de1a
```

`private/` は git 管理対象外。変更版 iPlug2 をこの公開レポへコピーしてコミットしない。

## 単体アプリ

```sh
cmake -S . -B build-mac-waki -G Xcode \
  -DIPLUG2_DIR="$PWD/private/iPlug2" \
  -DFIRSTSYNTH_FORMATS=APP \
  -DIPLUG_DEPLOY_PLUGINS=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build-mac-waki --config Debug --target FirstSynth-app --parallel 4
```

生成プロジェクト: `build-mac-waki/FirstSynth.xcodeproj`。
出力先: `build-mac-waki/out/Debug/FirstSynth.app`。
ローカルビルドの署名は無効。12.0 は配置対象の設定値で、旧 OS での動作確認済みを意味しない。
`-DCMAKE_OSX_DEPLOYMENT_TARGET` と `-DIPLUG_DEPLOY_PLUGINS=OFF` は CMakeLists の既定値と同じなので省略できる。

最低対応 macOS は 12.0（2026-09-22 に 11.3 から変更）。Xcode 27 の SDK は deployment target 12.0 未満を受け付けず、
11.3 のままでは新規の build ディレクトリでコンパイラ検出の段階で configure が止まる（既存の build ディレクトリは
コンパイラがキャッシュ済みなので気づかない）。9/12 の配布物は SDK 26.5 で作った 11.3 ビルドで、そのまま配布している。

configure は `config.h` の `PLUG_VERSION_STR` / `PLUG_VERSION_HEX` を真値として、macOS の Info.plist 4 本の版、
3 本の `LSMinimumSystemVersion`（実効の deployment target と比較）、AUv3 の `AudioComponents[0].version`（整数版）を検査し、
ずれていれば直し方を表示して止まる。Windows 側で版を上げたときに plist が取り残される事故（MediSynth 1.0.5 が
Logic に 1.0.3 と名乗った）を防ぐため。

## 回帰テスト

```sh
cmake -S eni_auth/tests -B build-mac-tests
cmake --build build-mac-tests
ctest --test-dir build-mac-tests --output-on-failure
IPLUG2_DIR="$PWD/private/iPlug2" ruby tests/storage_paths_test.rb
node tests/empty_preset_list_test.js
c++ -std=c++17 -I . tests/factory_presets_test.cpp -o /tmp/firstsynth-factory-test
ruby -rtmpdir -e 'Dir.mktmpdir("firstsynth-seed-") { |dir| abort "failed" unless system("/tmp/firstsynth-factory-test", dir) }'
bash tests/dist_mac_package_test.sh
```

保存パステストは本体のパス生成部分を実際にコンパイルし、iPlug2 の `INIPath` の返り値だけを一時フォルダへ差し替える。シリアライズや UI 経由の保存全体のテストではない。

配布の梱包テストは、アドホック署名した仮の bundle で `scripts/dist-mac.sh` の DMG 作成と引数のガードを動かす（18 件）。Developer ID もネットワークも要らない。2026-09-21 の事故（ZIP の展開で署名の封印が破れた）の回帰テストとして、拡張属性の付いた bundle が DMG を通っても壊れないことを確かめる。署名と公証そのものは確かめないので、実際の配布物は [04](04-macos-distribution.md) の手順で作る。

## 残る検証

- 全画面の表示・倍率変更。ログイン済みの状態で、設定を変更後に外部 MIDI コントローラーで発音できたとのユーザー確認あり。
- 日本語名でのプリセット保存・読込・削除と再起動時の設定復元。
- AUv2 / VST3 のビルドと DAW 内での検証。手元の VST3 SDK は README のみで、本体の導入が必要。

## 確認した結果

2026-09-12、Xcode / AppleClang 21 で Debug の arm64 単体アプリをビルド成功。`open` による起動要求も成功。UI 用の全 7 リソースと Info.plist の形式を確認。認証テストと保存パステストが成功。

依存ライブラリの整数変換・可変長配列とリンク時の重複 `-lobjc` の警告が残る。Windows での回帰確認は未実施。

単体アプリの MIDI 入力は初期状態で `off`。Preferences で接続した MIDI コントローラー（今回の実機は Arturia MiniLab mkII）と Audio Output を選択する。

macOS では `Resources/presets` の出荷用37プリセットを初回起動時に Application Support/FirstSynth/Presets へ配置する。同名ファイルを上書きせず、配置後の削除も尊重する。元データは共有レポ `origin/main` の `06fc5ab`。その後追加されたパラメーターの値は、このブランチのシンセでは適用されない。

プリセット/文字化け修正版のビルドと37ファイルの同梱は確認済み。終了操作がキャンセルされたため、ユーザー用フォルダへの初回配置と Audio Output 表示は手動再起動後の確認待ち。旧プロセスの settings.ini は文字化けしたままなので、修正版の Preferences で出力を選び直す。

## アプリアイコン

`resources/FirstSynth.iconset` の PNG から `iconutil -c icns resources/FirstSynth.iconset -o resources/FirstSynth.icns` で生成する。CMake はこの icns をアプリへ同梱する。

素材は mito@chronista.club の Google Drive、共有ドライブ `ANYCREATIVE Drive/easyandnice/firstsynth/icons` から取得。16/32/64/128/256 px は提供画像を使用し、512/1024 px は256 px画像を拡大したもの。高解像度原稿が届けば差し替える。
