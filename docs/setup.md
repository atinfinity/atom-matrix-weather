# 書き込みと初期設定

[← README](../README.md)

## 必要なもの

- M5Stack **ATOM Matrix**
- データ通信できる USB-C ケーブル
- (ブラウザから書き込む場合) PC 版の Chrome / Edge
- (自分でビルドする場合) Arduino IDE + M5Stack ボードマネージャ (esp32 core 3.x) と下記ライブラリ
  - M5Unified
  - ArduinoJson (v7)
  - Adafruit NeoPixel

## 書き込み

### ブラウザから書き込む (推奨)

Arduino IDE なしで、ビルド済みファームウェアをブラウザから直接書き込めます。

1. ATOM Matrix を USB で PC に接続する
2. PC 版の Chrome / Edge で **インストールページ** <https://atinfinity.github.io/atom-matrix-weather/> を開く
3. 「CONNECT」→ シリアルポートを選択 →「INSTALL」

- Mac で ATOM が認識されない場合は、USB シリアルドライバ (CH9102 / CH552) を [M5Stack のドライバページ](https://docs.m5stack.com/en/download) からインストールしてください
- 設定済みの機体を再インストールする場合、Wi-Fi 設定は保持されます。消したい場合は「Erase device」にチェックを入れてください
- `esptool` で書き込む場合は [Releases](https://github.com/atinfinity/atom-matrix-weather/releases) の `*.merged.bin` を offset `0x0` に書き込みます

### 自分でビルドする場合

`AtomMatrixWeather/AtomMatrixWeather.ino` を Arduino IDE で開き、ボード **M5Atom** を選んで書き込みます。
CI と同じ構成で `arduino-cli` を使う場合は [`.github/workflows/build.yml`](../.github/workflows/build.yml) を参照してください。

## 初期設定

1. 初回起動時は Wi-Fi 未設定のため自動で設定モードになり、LED が青く回転する
2. スマホ/PC から Wi-Fi `AtomWeather-XXXX` (パスワードなし) に接続する
3. 自動で設定ページが開かない場合は `http://192.168.4.1/` を開く
4. Wi-Fi SSID / パスワード / 都道府県を入力して「保存して再起動」
   - パスワード欄は常に空で表示されます。同じ SSID のまま空欄で保存すると前回のパスワードを維持します(SSID を変えた場合は空=パスワードなしとして保存)

設定は本体の NVS に保存され、電源を切っても保持されます。設定ページの下部に書き込まれているファームウェアのバージョンが表示されます。
