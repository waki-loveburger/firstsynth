# 04. macOS 配布物の署名・公証と DMG

単体アプリ + AUv3 の `FirstSynth.app` と `FirstSynth.vst3` を Developer ID で署名し、
Apple の公証を通して staple し、配布 DMG を作る。手順は `scripts/dist-mac.sh` にまとめてある。
配布物は DMG だけで、ZIP は作らない（後述）。

MediSynth の同名スクリプトからの移植。MediSynth 版は GitHub Releases 用に ZIP も作るが、
FirstSynth は DMG だけにした（2026-09-21、mako）。DMG を作る部分は、署名なしでテストできるよう関数に分けてある。

## 認証情報

- 署名: `Developer ID Application: Anycreative Inc. (3EQKG4B352)`（環境変数 `FIRSTSYNTH_SIGN_ID` で上書き可）
- 公証: Keychain profile `easyandnice-notary`（`FIRSTSYNTH_NOTARY_PROFILE` で上書き可）。MediSynth と共用
- パスワード・秘密鍵はリポジトリに保存しない

## 手順

先にビルドとローカル検証（[AUv3](02-macos-auv3-distribution.md) / [VST3](03-macos-vst3.md)）を済ませてから実行する。

```sh
bash tests/dist_mac_package_test.sh  # DMG の中身のテスト。署名・ネットワーク不要
scripts/dist-mac.sh app   # build-mac-auv3/out/Release/FirstSynth.app  → build-mac-auv3/dist/
scripts/dist-mac.sh vst3  # build-mac-vst3/out/Release/FirstSynth.vst3 → build-mac-vst3/dist/
```

スクリプトは次を順に行う。

1. `dist/` を作り直し、bundle をコピーする。`out/` の bundle には触れない
2. app は Framework → appex（sandbox entitlement 付き）→ app の順、VST3 は bundle を
   hardened runtime + timestamp 付きで署名し、`codesign --verify --deep --strict` で検査する
3. 提出用の ZIP を `notarytool submit --wait` で出し、`Accepted` でなければ止まる
4. `stapler staple` / `stapler validate`、app は `spctl --assess` も行う
5. bundle と `THIRD-PARTY-NOTICES.txt`（VST3 SDK の MIT ライセンス）、app には `/Applications` への
   リンクも入れて DMG を作り、DMG 自体を署名して公証・staple し、`spctl --type open` で
   Gatekeeper の判定を確認する。SHA-256 を書く

提出用の ZIP は公証に出すためだけに作り、終わったら消す。
成果物は `FirstSynth-<版>-macOS-arm64.dmg` と `FirstSynth-<版>-VST3-macOS-arm64.dmg`。
版は `config.h` の `PLUG_VERSION_STR` から取る。
配布のやり取りは共有ドライブ `ANYCREATIVE Drive/easyandnice/firstsynth/` で行い、そこには DMG を置く（2026-09-21、mako）。GitHub Releases は使わない（2026-09-22、mako）。

## なぜ DMG か

2026-09-21、斎藤さんの MacBook で FirstSynth が Gatekeeper に拒否された
（「マルウェアが含まれていないことを検証できませんでした」）。原因は 2 つ重なっていたと見ている。

- 共有ドライブに、配布しないはずの旧ビルド（初回の `build-mac-auv3/dist/`）が残っていた
- このスクリプト以前の手順は、配布 ZIP も `ditto -c -k --keepParent` で作っていた。
  拡張属性が AppleDouble `._*` として bundle の中に書き込まれ、Finder 以外
  （`unzip`、他の解凍ソフト、Windows 経由）で展開するとそのまま残り、署名の封印が破れる

DMG には展開工程が無いので、受け取る側の操作で壊れる余地が無い。MediSynth と共通の決定（mako）。
`--norsrc` を付ければ ZIP も壊れないが、共有に使わないものは作らない（取り違えの余地を残さない）。
共有ドライブの `easyandnice/` は、FirstSynth・MediSynth とも DMG だけにした（9/21）。
経緯と実測は creo の `mem_1CfGcTACNPbopUr5rq8TvW`（slug `eni-macos-dmg-distribution`）。

`tests/dist_mac_package_test.sh` はこの事故の回帰テストを含む。拡張属性の付いた bundle を
DMG に入れて取り出し、`._*` が無いことと署名が有効なことを確かめる。拡張属性を持てない
FAT32 で DMG を作ると、このテストが赤になることを確認済み。
`dist/` はビルドディレクトリの中にあり、ソース管理しない。

## 注意

- **同じ版の別物を 2 つ並べない**。どちらのビルドか見分けが付かなくなる（9/21 の事故の一因）。
  版を上げずに中身を差し替えるときは、共有ドライブの古い DMG を消して置き換え、SHA-256 を下の記録に残す
- `FirstSynth-auv3-local` を再実行すると `out/` の app はアドホック署名に戻る。スクリプトは
  `dist/` にコピーしてから署名するので影響しないが、`out/` を作り直したら配布物も作り直す
- 共有ドライブ上のファイルは CLI から読めないことがある（`Resource deadlock avoided`）。
  中身を確かめるときは Finder で「オフラインで使用可能にする」してから
- `hdiutil` は macOS 26 で非推奨の警告を出すが、動作する

## 受け取った人に伝えること

- 前の版の `FirstSynth.app` を先に削除する（ゴミ箱も空にする）。macOS 15 以降、
  署名が壊れた app は「システム設定 → プライバシーとセキュリティ → このまま開く」でも開けない
- app: DMG を開き、`FirstSynth.app` を `Applications` にドラッグする
- VST3: DMG を開き、`FirstSynth.vst3` を `~/Library/Audio/Plug-Ins/VST3/` にコピーして、ホストで再スキャンする

## 実機で確認すること

- DMG から入れた app を起動し、ホストの再起動後に AUv3 を選択できる
- `auval -v aumu hnve EZAN -oop` が通る
- DMG から入れた VST3 を Ableton Live で読み込める
- 別の Mac で共有ドライブから落とした DMG から起動・読み込みでき、Gatekeeper の警告が出ない

## 記録

2026-09-12、`v1.0.0-macos.1`（手作業。当時は ZIP で配布）:

| 成果物 | 公証 ID | 結果 |
|---|---|---|
| `FirstSynth.app`（Standalone + AUv3）初回 | `24f496a1-9c97-4691-a96b-152838e48fff` | Accepted（AUv3 初期サイズの不具合あり、配布しない） |
| `FirstSynth.app` 修正版 | `29181111-9494-4dc1-bb80-2ad93e4c84ad` | Accepted、staple、Gatekeeper `Notarized Developer ID`。GitHub のプレリリースに ZIP で公開（9/21 に ZIP を取り下げ、9/22 にリリース自体を削除。タグ `v1.0.0-macos.1` は残す） |
| `FirstSynth.vst3` | `a687536e-4be7-457f-a01b-338d9c4b5977` | Accepted、staple。共有していない |

2026-09-21、DMG 化（スクリプト化の前に手作業）:

| 成果物 | 公証 ID | 結果 |
|---|---|---|
| `FirstSynth-1.0.0-macOS-arm64.dmg`（中身は 9/12 の修正版 app） | `cafcfff3-6fe5-40f7-afac-962b5d1bd17c` | Accepted、staple、`spctl --type open` accepted |

この DMG を共有ドライブに置いた。SHA-256 は
`5833297beed1c7b84801ba1f597976f9c4dbb38dbc4d9219e8acc7a5051c1dc7`（同日 22:46 に下の DMG へ差し替え）。
同時に、共有ドライブにあった旧ビルドの ZIP と、GitHub プレリリースの ZIP を削除した。

2026-09-21、`scripts/dist-mac.sh` の初回（これが現在の配布物）:

| 成果物 | 公証 ID | 結果 |
|---|---|---|
| `FirstSynth.app` bundle | `10e4b04f-7898-44fc-9521-67025822bc4d` | Accepted、staple、Gatekeeper `Notarized Developer ID` |
| `FirstSynth-1.0.0-macOS-arm64.dmg` | `409ac3a6-5bfb-4a26-bc7a-38c21c16a0b1` | Accepted、staple、`spctl --type open` accepted |
| `FirstSynth.vst3` bundle | `d1b24e32-618c-4cfc-b938-2f694452c8ab` | Accepted、staple |
| `FirstSynth-1.0.0-VST3-macOS-arm64.dmg` | `70a09fb5-c9da-408f-a785-d82735298c4b` | Accepted、staple、`spctl --type open` accepted |

受け取る側の手順でも確認した。quarantine を付けた DMG から取り出した bundle は、署名・staple・Gatekeeper が有効で `._*` が無い。
配布 ZIP を `unzip` で展開しても同じ。
中身は 9/12 19:34 の Release ビルド（VST3 対応の後）で、それまで共有していた DMG（18:25 のビルド）とは別物。
mako の判断で、共有ドライブの app の DMG をこれに差し替え（同じ名前で上書きし、前の DMG は残していない）、
VST3 の DMG を初めて置いた。共有ドライブ上の SHA-256:

- `FirstSynth-1.0.0-macOS-arm64.dmg`: `27c3ddd0f371f3b2d675b466b5ed9d1c5305cdd9e408df712b3f58d42ec16480`
- `FirstSynth-1.0.0-VST3-macOS-arm64.dmg`: `8fcfb9b35ba440e3693b01bdb6ddcd33395b9a0b4d79df459ec4ea29d4b66d7e`

同日、ZIP の作成を外した版を VST3 で確認（出力先は作業用の場所。共有していない）。
bundle `1bf8cf79-44cf-4769-a3a0-ef6def22111f`、DMG `3312003f-6696-4f22-8c4e-f6c49b0cc3ce` とも Accepted、
出力は DMG と SHA-256 だけ。
