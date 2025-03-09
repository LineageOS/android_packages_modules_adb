# Delayed ACK

Historically, ADB transport protocol transfer speed was affected by two factors.

1. Each `A_WRTE` apacket was CRCed upon write and the CRC was checked upon read on the other end.
2. There could be only one `A_WRTE` apacket in-flight on an asocket. A local asocket
would not schedule more data to be sent out until it had received an `A_OKAY` apacket response from
its peer.

The first issue was solved in [aosp/568123](https://android-review.googlesource.com/q/568123).
In that CL, the protocol was updated to remove the requirement for CRC generation and verification.
This does not affect the reliability of a transport since both USB and TCP have packet checksums of their own.

The second issue is solved by "delayed ACK" ([aosp/1953877](https://android-review.googlesource.com/q/1953877)),
an experimental feature controlled by the environment variable `ADB_DELAYED_ACK`.

# How delayed ACK works

The idea is to introduce the concept of a per-asocket "available send bytes" (ASB) integer.
This integer represent how many bytes we are willing to send without having received any
`A_OKAY` for them.

While the ASB is positive, the asocket does not wait for an `A_OKAY` before sending
more `A_WRTE` apackets. A remote asocket can be written to up until the ASB is exhausted.

The ASB capability is first negotiated on `A_OPEN`/`A_OKAY` exchange. After
that, the ASB is maintained via decrement upon `A_WRTE` and increment
upon `A_OKAY`.

This approach allows to "burst" `A_WRTE` packet but also "burst" `A_OKAY` packets
to allow several `A_WRTE` packets to be in-flight on an asocket. This greatly
increases data transfer throughput.

# Implementation

## Packet update
1. `A_OPEN` unused field (`arg1`) is repurposed to declare the wish to use delayed ACK features.
If not supported, the receiving end of the `A_OPEN` will `A_CLSE` the connection.
2. `A_OKAY` now has a payload (a int32_t) which acknowledge how much payload was
received in the last received `A_WRTE` apacket.

## Trace

Here are two traces showing the timing of three A_WRTE.

### Before
```
Host                > A_OPEN                  > Device
Host                > A_WRTE                  > Device
The LS removes itself from the fdevent EPOLLIN and nothing is sent.
Host                < A_OKAY                  < Device
The LS requests fdevent EPOLLIN for its fd to start reading and send more A_WRTE.
Host                > A_WRTE                  > Device
The LS removes itself from the fdevent EPOLLIN and nothing is sent.
Host                < A_OKAY                  < Device
The LS requests fdevent EPOLLIN for its fd to start reading and send more A_WRTE.
Host                > A_WRTE                  > Device
The LS removes itself from the fdevent EPOLLIN and nothing is sent.
Host                < A_OKAY                  < Device
The LS requests fdevent EPOLLIN for its fd to start reading and send more A_WRTE.
```


## After

With ASB, see how `A_WRTE` and `A_OKAY` are burst instead of being paired.

```
Host(ASB=0)         > A_OPEN(arg1=1MiB)       > Device
Host(ASB=X)         < A_OKAY(<ASB=X>)         < Device
Host<ASB=X-a)       > A_WRTE(payload size=a)  > Device
Host<ASB=Y-a-b)     > A_WRTE(payload size=b)  > Device
Host<ASB=Z-a-b-c)   > A_WRTE(payload size=c)  > Device
ASB is < 0. The LS removes itself from the fdevent EPOLLIN and nothing is sent.
...
Host(ASB=X-b-c)     < A_OKAY(<a>)             < Device
ASB is > 0. The LS requests fdevent EPOLLIN for its fd to start reading and send more A_WRTE.
...
Host(ASB=X-c)       < A_OKAY(<b>)             < Device
Host(ASB=X)         < A_OKAY(<c>)             < Device
```


