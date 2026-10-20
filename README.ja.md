# RenkuOS 用 MacBook Touchpad

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md)

2010 年の MacBook7,1 (USB 05ac:0236) の生のタッチ座標を使うタッチパッド
ドライバです。1 本指の移動、物理クリック、ボタンを押したままのドラッグ、
2 本指の縦・横スクロールに対応します。

## インストール

```sh
pkgman add-repo https://pkgman.rainygirl.com/x86_64
pkgman refresh
pkgman install macbook_touchpad
```

インストール後に再起動してください。リポジトリが登録済みなら `add-repo` は
不要です。Haiku のパッケージ名にハイフンは使えないため、インストール名は
`macbook_touchpad` です。

x86_64 RenkuOS `r1~beta6_hrev60072_269_dirty` 専用です。`macbook_fnkey` が
依存パッケージとしてインストールされ、機能キーと明るさの修正を維持します。
他の Mac モデルや USB ID には適用しません。

## アンインストール

```sh
pkgman uninstall macbook_touchpad
```

削除後に再起動してください。`macbook_fnkey` の USB HID モジュールに戻ります。
インストールも削除も自動では再起動しません。

## ライセンス

MIT、[LICENSE](LICENSE) を参照。

## AI の利用について

MacBook Touchpad の一部は AI コーディングツール (OpenAI Codex および
Anthropic の Claude) の支援を受けて開発されました。すべてのコードは作者が
レビューし、テストしています。
