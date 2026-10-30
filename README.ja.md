# SplatThumb

[English](README.md)

3D Gaussian Splatting の `.ply` ファイルに、Windows エクスプローラーでサムネイルを表示するツールです。

> **現在は開発の初期段階です。** まだインストールできるものはありません。

SplatThumb はファイルのヘッダーを見て、3D Gaussian Splatting のシーンだけを描画します。
メッシュや点群の PLY には手を出さず、Windows の標準の表示に任せます。
ファイルは読むだけで書き換えず、ネットワーク通信もテレメトリーもありません。

## ソースからのビルド

必要なもの: Windows 10/11 x64、Visual Studio 2022(「C++ によるデスクトップ開発」。CMake 3.25 以上が含まれます)

「Developer PowerShell for VS 2022」を開き、リポジトリのフォルダで次を実行します。

```powershell
cmake --workflow --preset msvc-debug
```

設定、ビルド、テストまでまとめて実行されます。最適化したビルドは `msvc-release` を使います。
ビルドしただけでは Windows には何も登録されません。

## ライセンス

[MIT](LICENSE)
