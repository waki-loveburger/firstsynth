# 05. 公開用成果物の集約と、Cloudflare での公開

一般向けの配布は **R2 の公開リンク `https://dl.easyandnicewaki.com/firstsynth/...`**。
共有ドライブは開発側の置き場で、公開の前に版ごとに成果物を集約する場所として使う。
2026-11-20 の裁定（mako）で、MediSynth と同じ型に揃えた。MediSynth 側の正本は
あちらの非公開レポの `docs/guide/04-release-staging.md`。

| 置き場 | 役割 |
|---|---|
| 共有ドライブ `ANYCREATIVE Drive/easyandnice/firstsynth/` | 開発側のやり取り。公開前の集約 `releases/<版>/` |
| R2 バケット `easyandnice-dl` → `dl.easyandnicewaki.com` | 一般向け。ログイン不要で誰でも落とせる |
| GitHub Releases | 使わない（2026-09-22、mako。2026-11-20 も変更なし） |

公開リンクで構わないのは、アプリ自身が起動時に eni_auth でログインと購読を確かめるから。
配布物を手に入れただけでは音が出ない。

**Drive に置いた時点では公開ではない。** 公開は R2 に上がって `latest` が向いてから。

## 1. 版ごとに Drive へ集約する

```text
releases/1.0.0/
  mac/           # 公証・staple 済み DMG 2 本と SHA256SUMS.txt
  windows/       # インストーラーと SHA256SUMS.txt
  SHA256SUMS.txt # 両 OS が揃ってから作る
```

自分の PC の Drive 同期フォルダを `FIRSTSYNTH_DRIVE_DIR` に入れておく（`easyandnice/firstsynth` まで）。
既にある旧版のファイルは動かさない。同名で中身が違うファイルがあると、スクリプトが上書きせずに止まる。

### Mac

[04](04-macos-distribution.md) の手順で署名・公証・staple・DMG まで済ませてから:

```sh
version=$(python3 -c 'import re;print(re.search(r"PLUG_VERSION_STR.*?([0-9]+[.][0-9]+[.][0-9]+)", open("config.h").read())[1])')
python3 scripts/stage-release.py --root "$FIRSTSYNTH_DRIVE_DIR" --version "$version" --platform mac \
  build-mac-auv3/dist/*.dmg build-mac-vst3/dist/*.dmg
```

### Windows（PowerShell）

`installer/README.md` に従ってインストーラーを作り、実際に入れて確かめた後:

```powershell
$releaseDrive = 'G:\共有ドライブ\ANYCREATIVE Drive\easyandnice\firstsynth'
python scripts/stage-release.py --root $releaseDrive --version 1.0.0 --platform windows installer/installer_output/FirstSynth_setup_v1.0.0.exe
```

`python` が Microsoft Store のスタブだと「Python」と出して何もせず終わる（終了コード 49）。
そのときは `py -3` か `uv run --no-project python` に読み替える。

### 両方が同期された後

mac / windows の双方が見える PC で一度だけ:

```sh
python3 scripts/stage-release.py --root "$FIRSTSYNTH_DRIVE_DIR" --version "$version" --finalize
```

各 OS のチェックサムを照合し、全体の `SHA256SUMS.txt` を作る。片方が未配置・未同期なら失敗する。
片方の OS だけを出し直す版では `--finalize` を使わない（その OS の `SHA256SUMS.txt` で照合する）。

## 2. R2 に上げる

バケットとドメインは easy and nice の Cloudflare アカウント（脇坂さん）にある。
**このレポは公開なので、アカウント ID とゾーン ID はここに書かない。**
値は MediSynth（非公開レポ）の `docs/guide/04-release-staging.md` と creo
`mem_1CgVMvmMzSi8wDUAeyjmdu`（2026-10-29 の裁定）にある。
`cf` CLI の入れ方（bun 経由。node 22.23 以降が要るため）もあちらに書いてある。

```sh
export CLOUDFLARE_ACCOUNT_ID=...   # easy and nice のアカウント
version=1.0.0
src="$FIRSTSYNTH_DRIVE_DIR/releases/$version"
for f in "$src"/mac/* "$src"/windows/* "$src"/SHA256SUMS.txt; do
  rel=${f#"$src"/}
  case "$rel" in
    *.dmg) ct=application/x-apple-diskimage ;;
    *.exe|*.msi) ct=application/octet-stream ;;
    *.zip) ct=application/zip ;;
    *.txt) ct='text/plain; charset=utf-8' ;;
    *) echo "skip: $rel"; continue ;;
  esac
  cf r2 objects put "firstsynth/$version/$rel" --bucket-name easyandnice-dl --file "$f" --content-type "$ct" || break
done
```

Drive の版フォルダ直下に手で置いた内部用のファイルは上げない。1 ファイル 300 MB まで。
**公開した版のファイルは差し替えない。** DMG・exe は Cloudflare の各拠点に最長 2 時間、
利用者のブラウザに最長 4 時間残るので、差し替えても古いものがしばらく配られ、
キャッシュされない `SHA256SUMS.txt` と合わなくなる。直すときは版を上げる。

## 3. 落として照合する

公開 URL から空のフォルダへ落として、全体の `SHA256SUMS.txt` で照合する。

```sh
version=1.0.0
mkdir -p "firstsynth-dl-$version" && cd "firstsynth-dl-$version"
base="https://dl.easyandnicewaki.com/firstsynth/$version"
curl -fsSO "$base/SHA256SUMS.txt"
for rel in $(awk '{print $2}' SHA256SUMS.txt); do curl -fsS --create-dirs -o "$rel" "$base/$rel" || break; done
shasum -a 256 -c SHA256SUMS.txt
```

Windows の Git Bash では最後の行を `sha256sum -c SHA256SUMS.txt` にする。
フォルダの URL（`…/firstsynth/1.0.0/`）は 404 で、一覧は見えない。

Claude Code のシェルから `curl` を叩くときは `PATH="/usr/bin:$PATH"` を付ける（brew の shim で空振りする）。

## 4. latest を新しい版に向ける

portal からは版の入らない URL にリンクし、ゾーンのリダイレクトルールで最新版へ 302 で転送する。
落ちてくるファイル名には版が入る。

| URL | 転送先（`<v>` は最新版） |
|---|---|
| `https://dl.easyandnicewaki.com/firstsynth/latest/windows` | `firstsynth/<v>/windows/FirstSynth_setup_v<v>.exe` |
| `https://dl.easyandnicewaki.com/firstsynth/latest/mac` | `firstsynth/<v>/mac/FirstSynth-<v>-macOS-arm64.dmg` |
| `https://dl.easyandnicewaki.com/firstsynth/latest/mac-vst3` | `firstsynth/<v>/mac/FirstSynth-<v>-VST3-macOS-arm64.dmg` |

**このルールセットはゾーンに 1 つで、MediSynth（`medisynth_latest_*`）と同居する。**
丸ごと書き直すと、もう一方の製品のダウンロードが落ちる。`scripts/r2-latest-rules.py` は
今のルールセットを読んで、`firstsynth_latest_*` 以外を**そのまま残し**、自分の 3 本だけ差し替えた
本文を作る。ルールの一覧が見つからない入力は、空のルールセットとして扱わずにエラーにする
（検査は `tests/r2_latest_rules_test.py`）。

アップロードと照合が済んでから:

```sh
export CLOUDFLARE_ACCOUNT_ID=...
zone=...                                   # easyandnicewaki.com のゾーン ID
cf rulesets entrypoint get http_request_dynamic_redirect -z $zone > current-rules.json
python3 scripts/r2-latest-rules.py --current current-rules.json --version 1.0.0 --out latest-rules.json
cf rulesets entrypoint update http_request_dynamic_redirect -z $zone --body @latest-rules.json
for s in windows mac mac-vst3; do
  curl -sSIL -o /dev/null -w "$s %{http_code} %{url_effective}\n" "https://dl.easyandnicewaki.com/firstsynth/latest/$s"
done
```

本文は `--body` で渡す（`--rules` だと cf が ruleset の `--id` を要求して止まる）。
先に試すなら `update` の行に `--dry-run` を付ける。
最新版を `latest` という固定のファイル名で置き直す方法は採らない（キャッシュのせいで古い版が配られる）。

> MediSynth 側の手順は「ルールセットを丸ごと書き直す」形で、他製品のルールを見つけると止まる安全弁が付いている。
> FirstSynth のルールが入った後は、あちらも止まるようになる。MediSynth の次の公開までに、
> あちらの手順もこのスクリプトと同じ「残して差し替える」形へ直す。

## 5. 公開の仕上げ

- **eni-api**: `products` の `firstsynth` に `released_at` を入れる（`0002_license.sql` では NULL のまま）。
  MediSynth の `0003_medisynth.sql` と同じ形の migration を足して本番の D1 に当てる
- **portal**: `1st-synth/index.html` の DOWNLOAD を、上の `latest` の 3 本に差し替える。
  Windows が未署名のままなら SmartScreen の案内を添える（MediSynth の `medi-synth` と同じ）
- **portal**: `instruments.html` のカードを「準備中」から外す

## 記録

| 版 | 公開日 | 置いたもの | 備考 |
|---|---|---|---|
| — | — | — | 1.0.0 の公開はこれから |
