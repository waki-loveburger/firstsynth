# 03. macOS VST3

Apple Silicon / macOS 11.3 以降を対象とする。Logic は AUv3 を使用し、VST3 は Ableton Live などの VST3 ホストで使用する。

## ビルド

`01-macos-build.md` と同じ変更版 iPlug2 を使用する。VST3 SDK は公式
`steinbergmedia/vst3sdk` の `v3.8.1_build_84`
（`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`）を使用する。

```sh
git clone --branch v3.8.1_build_84 --depth 1 --recurse-submodules --shallow-submodules \
  https://github.com/steinbergmedia/vst3sdk.git private/vst3sdk
cp -R private/vst3sdk/base private/vst3sdk/pluginterfaces private/vst3sdk/public.sdk \
  private/iPlug2/Dependencies/IPlug/VST3_SDK/
cmake -S . -B build-mac-vst3 -G Xcode \
  -DIPLUG2_DIR="$PWD/private/iPlug2" -DFIRSTSYNTH_FORMATS=VST3 \
  -DIPLUG_DEPLOY_PLUGINS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3
cmake --build build-mac-vst3 --config Release --target FirstSynth-vst3 --parallel 4
```

成果物は `build-mac-vst3/out/Release/FirstSynth.vst3`。
SDK と変更版 iPlug2 は `private/` に置き、FirstSynth の公開リポジトリへ追加しない。
Info.plist を変更した場合は configure から実行する。

## 自動検証

```sh
cmake -S private/vst3sdk -B build-mac-vst3-sdk -G Xcode \
  -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF
cmake --build build-mac-vst3-sdk --config Release --target validator --parallel 4
build-mac-vst3-sdk/bin/Release/validator build-mac-vst3/out/Release/FirstSynth.vst3
bash tests/run_vst3_view_test.sh
```

画面テストは実際の VST3 をロードして NSView に接続し、WebKit で全4ページの表示領域とホストからのリサイズを検証する。GUI セッションが必要。
validator の成功は認証後の発音確認の代わりにはならない。

## Ableton Live で確認

1. `FirstSynth.vst3` を `~/Library/Audio/Plug-Ins/VST3/` に配置する。
2. Live の設定 → Plug-Ins で VST3 のシステムフォルダを有効にして再スキャンする。
3. ブラウザの VST3 → EASYANDNICE → FirstSynth を MIDI トラックに挿す。
4. 認証のキャンセル、再ログイン、外部 MIDI による発音、全ページの拡大縮小を確認する。
5. プリセット選択・パラメータ変更後に Live Set を保存し、再度開いて復元を確認する。
6. 複数インスタンスと、再生中のエディタの開閉を確認する。

初期サイズは約80%（1110×606）。ホストのリサイズは 1387:757 の比率で約50〜150%に制限する。
Web UI の Zoom 選択は隠し、ホストのサイズを使う。Windows VST3 の従来動作は維持する。
VST3 と単体アプリは通常のユーザー保存領域を共有するため、既にログイン済みの場合がある。

## 配布

未署名のビルドをそのまま公開しない。Developer ID 署名と公証を行い、SDK の MIT ライセンスを配布物に添える。
SDK 3.8 以降は MIT ライセンスで、SDK の利用にライセンスキーの埋め込みは不要。
旧契約の扱いや VST 商標・ロゴの利用条件は別途その条件に従う。
公式情報: https://www.steinberg.net/ja/press/2025/vst-3-8/

署名・公証・配布物の作成は [04. 配布](04-macos-distribution.md) の `scripts/dist-mac.sh vst3` で行う。
`THIRD-PARTY-NOTICES.txt`（SDK の MIT ライセンス）を添えた DMG ができる。共有ドライブに置くのは DMG。
VST3 を変更したら、署名から DMG の作成までをすべてやり直す。

## 検証記録

- 2026-09-12: Release / arm64 ビルド成功。公式 validator 47 件成功・0 件失敗。
- 実 VST3 の WebKit テストで全4ページの表示、900 / 1110 / 1387 px のホストリサイズ、縦横比制約、エディタの開閉を確認。
- 共通 UI を使う AUv3 の表示回帰テスト、空プリセット一覧テスト、eni_auth テスト成功。
- Developer ID 署名、公証 Accepted（`a687536e-4be7-457f-a01b-338d9c4b5977`）、staple / validate 成功。
- `~/Library/Audio/Plug-Ins/VST3/FirstSynth.vst3` へ配置済み。Ableton Live でユーザーが読み込みと LOOPER の録音・ループ再生を確認。認証キャンセル・Live Set の保存復元は実機確認待ち。

## 既知の問題

Ableton Live の VST3 版で、LOOPER を録音して再生中にエディタを閉じ、再度開くと、音声のループは継続するが LOOPER の表示が初期状態へ戻る。ユーザーの指示により waki さんの修正担当とし、この変更では修正しない。自動テストのエディタ開閉確認は、録音後の LOOPER 表示復元までは検証していない。
