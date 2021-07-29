Name 
====

kcptun-asio -- A Tunnel Based On KCP with N:M Multiplexing  
kcptun-asio is based on C++11 and Asio, it's not exactly compatible with [kcptun(go)](https://github.com/xtaci/kcptun)  

Synopsis
========

```
$ ./kcptun_client -l :6666 -r xx:xx:xx:xx:yy --mtu 1200 --ds 20 --ps 10
$ ./kcptun_server -l :7777 -t xx:xx:xx:xx:yy --mtu 1200 --ds 20 --ps 10
```
```
./kcptun_server -l :6666 -t 127.0.0.1:2935
./kcptun_client -l :3935 -r 127.0.0.1:6666
```

Features
========

* reliable data transfering based on kcp protocol  
* multiplexing  
* lower resource consumption  

removed features from origin [kcptun-asio](https://github.com/ccsexyz/kcptun-asio) 
* support aes*/xor/xtea/none/cast5/blowfish/twofish/3des/salsa20 encryption 
* snappy streaming compression and decompression,based on [google/snappy](https://github.com/google/snappy).The data frame format is [frame_format](https://github.com/google/snappy/blob/master/framing_format.txt)  
* forward error correction  

Build
=====

Prerequisites
-------------

1. [asio](https://github.com/chriskohlhoff/asio)

Unix-like system
----------------
1. Get the latest code  
```
$ git clone https://github.com/rise/kcptun-asio.git  
```
2. Run build.sh
```
$ ./build.sh  
```

odd Windows
-----------

Fuck MSVC.
 
TODO   
====

* performance optimization(memory optimization & CPU optimization)   
* ~~improve smux~~   
