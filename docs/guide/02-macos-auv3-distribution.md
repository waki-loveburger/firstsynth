# 02. macOS AUv3

FirstSynth.app に FirstSynthAUv3.appex と FirstSynthAU.framework を同梱する。
2026-09-12 に Apple Silicon 向けの検証用プレリリース `v1.0.0-macos.1` を作った。
2026-09-12、Logic Pro で認証キャンセル・サイズ調整・比例拡縮の動作をユーザーが確認。
署名・公証・配布物（DMG）の作成は [04. 配布](04-macos-distribution.md) の `scripts/dist-mac.sh app` で行う。

音源が画面より後に初期化されると preferredContentSize が0のままになる問題は
`tests/auv3_view_test.mm` で再現し、専用 view controller でメインスレッドにサイズを通知して修正。
AUv3 の基準は1387×757、初期表示は約80%の1110×606。ホストが変えた枠を
frame の変更通知から C++ の `OnParentWindowResize`、Web UI の `FitAUv3ToSize` へ渡す。
Web UI は一定の配置を transform で比例拡縮する。AUv3 では個別の Zoom 選択を隠す。
アプリ内 WebKit のテストで全ページの収まりと900/1110pxを6回往復する拡縮、
画面先行・音源先行の両方の初期化順が成功。auval も再成功。
Logic の右下ドラッグと認証キャンセル後の操作はユーザー確認済み。

UI テスト（ウィンドウを前面に表示する）:

```sh
clang++ -fobjc-arc tests/auv3_view_test.mm -framework AppKit -framework AudioToolbox \
  -framework CoreAudioKit -framework WebKit -o /private/tmp/firstsynth-auv3-view-test
/private/tmp/firstsynth-auv3-view-test "$PWD/build-mac-auv3/out/Release/FirstSynthAU.framework"
/private/tmp/firstsynth-auv3-view-test "$PWD/build-mac-auv3/out/Release/FirstSynthAU.framework" --audio-first
```

2026-09-12 の検証結果:

- Release ビルド成功、署名前後の `auval -v aumu hnve EZAN -oop` 成功。
- アプリ・framework 各37個のプリセットと Web UI の同梱を確認。
- AUv3 sandbox 内にも37個の初回プリセット生成を確認。
- 修正版の公証 `Accepted`: `29181111-9494-4dc1-bb80-2ad93e4c84ad`。
- `stapler validate` 成功、Gatekeeper は `accepted / Notarized Developer ID`。
- Logic の認証キャンセル・サイズ調整・比例拡縮はユーザー確認済み。
- ladyland と DAW のプロジェクト保存・復元は引き続き確認待ち。

## ビルド

```sh
cmake -S . -B build-mac-auv3 -G Xcode \
  -DIPLUG2_DIR="$PWD/private/iPlug2" \
  '-DFIRSTSYNTH_FORMATS=APP;AUV3' \
  -DIPLUG_DEPLOY_PLUGINS=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3
cmake --build build-mac-auv3 --config Release \
  --target FirstSynth-auv3-local --parallel 4
```

成果物は `build-mac-auv3/out/Release/FirstSynth.app`。
配布・AUv3 検証には Release を使う。現状の Debug は Web UI をソースツリーから読む。
`FirstSynth-auv3-local` は毎回拡張を再同梱し、内側から ad-hoc 署名して構造を検証する。
`scripts/dist-mac.sh` は `dist/` にコピーしてから Developer ID で署名するので、ここでの ad-hoc 署名は配布物に影響しない。
`out/` を作り直したら配布物も作り直す。

AUv3 の識別子は `com.EASYANDNICE.app.FirstSynth.AUv3`、コンポーネントは
`aumu hnve EZAN`。ホストでは `EASYANDNICE: FirstSynth` として表示される。
37 個の factory preset と Web UI は拡張が使う framework にも同梱する。

## 署名・公証・配布

[04. 配布](04-macos-distribution.md) の `scripts/dist-mac.sh app` で行う。共有ドライブに置くのは DMG。
以前ここにあった手順は配布 ZIP を `ditto -c -k --keepParent` で作っており、
Finder 以外で展開すると署名が壊れた（2026-09-21 に Gatekeeper で拒否された原因の一つ）。手で ZIP を作って配布しない。

## 実機で確認すること

- アプリを Applications に配置して起動し、ホストを再起動後 AUv3 を選択できる。
- `auval -v aumu hnve EZAN -oop` が通る。
- AUv3 の Web UI、ログイン、MIDI 発音、プリセット切替、プロジェクト保存・復元。
- AUv3 は sandbox 内で動くため、単体版のログイン・ユーザープリセットの共有は未保証。
  自動共有が必要な場合は App Group の設計が必要。
- 共有ドライブから落とした DMG で、別 Mac でも起動・読み込みできる。
