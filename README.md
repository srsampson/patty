#### patty - Userland AX.25 stack
An AX.25 V2.2 Stack Outside of the Kernel

Original: https://scm.xan.host

#### Build
sh configure   
make   
sudo make install   
sudo ldconfig   

#### Examples
**/etc/patty/pattyd.conf**
```
sock /var/run/patty/patty.sock
pid /var/run/patty/patty.pid
if kiss0 ax25 K5OKC kiss /dev/tty21 baud 19200
route default if kiss0
```
```
connect /var/run/patty/patty.sock K5OKD
```
```
ax25dump -i kiss0
```
```
K5OKC>K5OKD (XID cmd P/F=0)
    XID parameters:
        Classes of procedures:
            > ABM
            > half-duplex
        HDLC optional functions:
            > REJ
            > SREJ
            > extended addresses
            > modulo 128
            > TEST
            > 16-bit FCS
            > synchronous TX
            > multiple SREJ
        I Field Length RX: 1536
        Window Size RX: 127
        Ack timer: 3000
        Retry: 10
00000000: 966a 9e96 8840 e096 6a9e 9686 4061 af82  .j...@..j...@a..
00000010: 8000 1702 0221 0003 0386 a822 0602 3000  .....!....."..0.
00000020: 0801 7f09 020b b80a 010a                 ..........
K5OKC>K5OKD (SABM cmd P/F=1)
00000000: 966a 9e96 8840 e096 6a9e 9686 4061 3f    .j...@..j...@a?
K5OKC>K5OKD (SABM cmd P/F=1)
00000000: 966a 9e96 8840 e096 6a9e 9686 4061 3f    .j...@..j...@a?
K5OKC>K5OKD (SABM cmd P/F=1)
00000000: 966a 9e96 8840 e096 6a9e 9686 4061 3f    .j...@..j...@a?
```

