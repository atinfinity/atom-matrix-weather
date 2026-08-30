# AtomMatrixWeather

M5Stack **ATOM Matrix** の 5x5 LED に、今日の午前・午後の天気を色で表示するスケッチです。
天気データは [Open-Meteo](https://open-meteo.com/) から取得します(無料・API キー不要)。

![動作イメージ](docs/overview.svg)

- 左2列 = 午前 (6〜12時)、右2列 = 午後 (12〜18時)。色は 晴=オレンジ / 曇=グレー / 雨=青 / 雪=白 / 雷雨=黄
- Wi-Fi と都道府県は本体の設定モード (AP + 設定ページ) からスマホ/PC で設定。スケッチに直書きしません
- 30 分ごとに自動更新。Wi-Fi 切断時はバックグラウンドで自動再接続

## クイックスタート

1. ATOM Matrix を USB で PC に接続し、PC 版 Chrome / Edge で **インストールページ** <https://atinfinity.github.io/atom-matrix-weather/> を開いて書き込む (Arduino IDE 不要)
2. 初回起動で設定モード (LED が青く回転) になるので、スマホ/PC から Wi-Fi `AtomWeather-XXXX` に接続する
3. 開いた設定ページで Wi-Fi SSID / パスワード / 都道府県を入力して「保存して再起動」

詳しい手順やトラブルシュートは [書き込みと初期設定](docs/setup.md) を参照してください。

## ドキュメント

| ドキュメント | 内容 |
|---|---|
| [書き込みと初期設定](docs/setup.md) | 必要なもの、ブラウザからの書き込み、自分でビルドする方法、初期設定 |
| [表示と操作](docs/display.md) | LED のレイアウト・色・ステータス表示・回転アニメーション、ボタン操作 |
| [開発者向け情報](docs/development.md) | 調整できる定数、シリアルログ、CI とリリース手順 |

## ライセンス

MIT License
