# Socket_C

## Overview
C言語で記述したソケット通信プログラムです．
Soxを用いた双方向の通信が可能となっています．

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
./Socket_phone <IP_Addr> <Poirt>
```
(IP_Addr)および(Port)にはサーバー側のIPアドレス，ポート番号を入力する．
サーバーへの接続を試みる．