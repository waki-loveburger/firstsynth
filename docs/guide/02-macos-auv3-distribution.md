# macOS AUv3 のビルドと公証

FirstSynth.app に FirstSynthAUv3.appex と FirstSynthAU.framework を同梱する。
Apple Silicon 向けの検証用プレリリース `v1.0.0-macos.1`。
2026-09-12、Logic Pro で認証キャンセル・サイズ調整・比例拡縮の動作をユーザーが確認。
初回の公証済み `build-mac-auv3/dist/` は不具合を含む旧ビルドのため配布しない。
修正版の配布成果物は `build-mac-auv3/dist-macos.1/` に保存する。

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
このターゲットを再実行すると Developer ID 署名も置き換わるので、公証後は再ビルドしない。

AUv3 の識別子は `com.EASYANDNICE.app.FirstSynth.AUv3`、コンポーネントは
`aumu hnve EZAN`。ホストでは `EASYANDNICE: FirstSynth` として表示される。
37 個の factory preset と Web UI は拡張が使う framework にも同梱する。

## 認証

署名: `Developer ID Application: Anycreative Inc. (3EQKG4B352)`。
公証: Keychain profile `easyandnice-notary`。MediSynth と共用する。
パスワード・秘密鍵はリポジトリに保存しない。

## 署名と公証

ローカル AU 検証後、配布用コピーを作って以下を実行する。
`dist` 配下はビルド用ディレクトリの中に置き、ソース管理しない。

```sh
mkdir -p build-mac-auv3/dist-macos.1
ditto build-mac-auv3/out/Release/FirstSynth.app build-mac-auv3/dist-macos.1/FirstSynth.app
FIRSTSYNTH_APP="$PWD/build-mac-auv3/dist-macos.1/FirstSynth.app"
FIRSTSYNTH_SIGN_ID='Developer ID Application: Anycreative Inc. (3EQKG4B352)'
codesign --force --timestamp --options runtime --sign "$FIRSTSYNTH_SIGN_ID" \
  "$FIRSTSYNTH_APP/Contents/Frameworks/FirstSynthAU.framework"
codesign --force --timestamp --options runtime --sign "$FIRSTSYNTH_SIGN_ID" \
  --entitlements resources/FirstSynth-AUv3.entitlements \
  "$FIRSTSYNTH_APP/Contents/PlugIns/FirstSynthAUv3.appex"
codesign --force --timestamp --options runtime --sign "$FIRSTSYNTH_SIGN_ID" "$FIRSTSYNTH_APP"
codesign --verify --deep --strict "$FIRSTSYNTH_APP"
ditto -c -k --keepParent "$FIRSTSYNTH_APP" build-mac-auv3/dist-macos.1/FirstSynth-submit.zip
xcrun notarytool submit build-mac-auv3/dist-macos.1/FirstSynth-submit.zip \
  --keychain-profile easyandnice-notary --wait
```

`Accepted` を確認してから ticket を添付し、配布 ZIP を作り直す。

```sh
xcrun stapler staple "$FIRSTSYNTH_APP"
xcrun stapler validate "$FIRSTSYNTH_APP"
spctl --assess --type execute --verbose=2 "$FIRSTSYNTH_APP"
ditto -c -k --keepParent "$FIRSTSYNTH_APP" build-mac-auv3/dist-macos.1/FirstSynth-macOS-arm64.zip
```

## 実機で確認すること

- アプリを Applications に配置して起動し、ホストを再起動後 AUv3 を選択できる。
- `auval -v aumu hnve EZAN -oop` が通る。
- AUv3 の Web UI、ログイン、MIDI 発音、プリセット切替、プロジェクト保存・復元。
- AUv3 は sandbox 内で動くため、単体版のログイン・ユーザープリセットの共有は未保証。
  自動共有が必要な場合は App Group の設計が必要。
- ダウンロードした成果物を別 Mac で起動・読み込みできる。
