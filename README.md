# Socket_C

## Overview
C言語で記述したソケット通信プログラムです．
Soxを用いた双方向の通信が可能となっています．

<img width="1020" alt="FLOW" src="https://github.com/Haruk1y/Socket_C/assets/147027077/d96d4aac-1714-4d70-ae53-a4b15ccd2a3f">

## Requirement
Sox

## Usage
- コンパイル
```sh
gcc -o Socket_phone Socket_phone.c
```
でコンパイル．

- サーバー側
```sh
./Socket_phone <Port>
```
(Port)には好きなポート番号を入れる．サーバーが立ちあがり，
クライアントの接続待ち状態になる．

- クライアント側
```sh
./Socket_phone <IP_Addr> <Port>
```
(IP_Addr)および(Port)にはサーバー側のIPアドレス，ポート番号を入力する．
サーバーへの接続を試みる．

## Feature
下記機能の実現．
- 擬似受話器
- 着信音の再生
- 留守番電話
- ボイスチェンジャー
- ノイズキャンセル
- ミュート機能
- 消音機能
