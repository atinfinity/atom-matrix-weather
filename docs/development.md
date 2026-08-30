# 開発者向け情報

[← README](../README.md)

## 定数 (スケッチ冒頭で変更可能)

| 定数 | 既定値 | 説明 |
|---|---|---|
| `UPDATE_INTERVAL_MS` | 30 分 | 天気の取得間隔 |
| `LED_BRIGHTNESS` | 90 | LED の明るさ (0-255) |
| `ROTATE_180` | 0 | 1 で表示を 180 度回転 (USB を上にして置く場合) |
| `WIFI_CONNECT_TIMEOUT_MS` | 20 秒 | Wi-Fi 接続待ちの上限 |
| `LONG_PRESS_MS` | 3 秒 | 設定モードに入る長押し時間 |
| `WIFI_RETRY_INITIAL_MS` | 10 秒 | 再接続バックオフの初期間隔 (失敗ごとに2倍) |
| `WIFI_RETRY_MAX_MS` | 5 分 | 再接続バックオフの上限 |
| `FETCH_RETRY_INITIAL_MS` | 1 分 | 取得失敗時の再試行間隔の初期値 (失敗ごとに2倍) |
| `FETCH_RETRY_MAX_MS` | 10 分 | 取得失敗時の再試行間隔の上限 |

## シリアルログ

115200 baud。設定値、取得結果 (午前/午後の判定)、エラー内容を出力します。

## 備考

- HTTPS の証明書検証は省略しています (`setInsecure()`)
- 天気の取得に失敗した場合 (タイムアウトなど) は 1分→2分→…→最大10分の間隔で再試行し、成功したら通常の取得間隔に戻ります
- Wi-Fi が切断されると自動で再接続を試みます (10秒→20秒→…→最大5分の間隔)。再接続後、取得間隔を超過していれば即時取得します
- 日付の切り替わりは定期取得のたびに「今日」を要求することで自然に追従します (最大で取得間隔分の遅れ)

## CI

GitHub Actions ([`build.yml`](../.github/workflows/build.yml)) が `arduino-cli` で固定バージョンのライブラリを使ってビルドします。main への push / PR ではコンパイルチェックのみ行い、生成物 (`*.bin`) は Actions の artifact から取得できます。

## リリース手順

`v*` タグを push すると GitHub Actions がビルドし、Release の作成と GitHub Pages (インストールページ) の更新を行います。

```sh
git tag v1.0.0
git push origin v1.0.0
```

リリース時のチェックリストは [Issue #10](https://github.com/atinfinity/atom-matrix-weather/issues/10) を参照してください。
