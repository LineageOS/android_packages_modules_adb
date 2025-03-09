# How ADB uses USB Zero-Length Packets (ZLP)

## TLDR;
There is no USB mechanism that lets a sender announce the size of a `Transfer`. This is not
a problem when the host side receives packet since is leverage the aprotocol to know what size
of transfer to expect. However, traffic towards the device must include zero length packets to
mark the boundaries since the device does not leverage the aprotocol.

## Introduction

There is an asymmetry in how ADB communicates over USB. While all USB backends on the host side (Linux,
Windows, Mac, and libusb) send ZLPs, the device side never does. Why is that? This document explains
what ZLPs are, how ADB uses them, and why things are designed this way.

## USB Transfer 101

In the context of ADB, USB can be thought of as two unidirectional pipes per device.
One pipe takes care of upload while the other takes care of the download. On the pipe
transit payloads. The maximum size of a payload is set by the pipe buffers located
on the device. These buffers are called the `Endpoint`.

```
  ┌──────────┐                         ┌──────────┐
  │ USB Host │                         │USB Device│
  ├──────────┤                         ├──────────┤
  │          │                         │          │
  │  Pipe ◄──┼─────────────────────────┼ Endpoint │
  │          │        USB              │          │
  │  Pipe ───┼─────────────────────────► Endpoint │
  └──────────┘                         └──────────┘
```

In USB parlance, sending a buffer of data on a pipe and receiving it on the other end is called a `Transfer`.
On the sender side, the USB Controller is presented with a [buffer,size] pair called IRP. On the receiver
side, a similar IRP is provided to the USB controller.

```
       ┌────────┐                             ┌──────────┐
       │ Sender │                             │ Receiver │
       └────┬───┘                             └─────┬────┘
            │                                       │
         ┌──▼───┐                                ┌──▼───┐
         │ IRP  │                                │  IRP │
         └──┬───┘                                └──▲───┘
     ┌──────▼───────┐                        ┌──────┴───────┐
     │USB Controller│                        │USB Controller│
     │              │                        │              │
     │             ─┼───►─DP──►─DP ─►──DP──►─┼─►            │
     └──────────────┘                        └──────────────┘
```

Because of the endpoint buffer size (`wMaxPacketSize`), an IRP is broken down in
several data payloads called `Data Packets` (DP).

Note: On the device, ADB uses `functionfs` which is not based on IRP. However, the logic is the same since received DP
must be re-assembled on the device to rebuild the original IRP. To simplify this document we use the name "IRP"
everywhere in this doc to mean "[buffer,size] pair provided to the USB Controller".

## When does a USB Transfer ends?

If an IRP is broken down in DPs by the sender, how does the receiver reassemble
the DPs into an IRP on the other side?

The key concept to get out of this whole document is that there is no mechanism
in USB for the sender to announce the size of a `Transfer`. Instead, the receiving
end uses the following rules. A `Transfer` is considered done when either of the following condition
is met.

- An error occurred (device disconnected, ...).
- The IRP is full.
- The size of the packet is less than `wMaxPacketSize` (this is a Short-Packet). This is
a different behavior from the usual UNIX `read(3)` which is allowed to return less than required
without meaning that the stream is over.
- Too much data is received. The IRP overflows (this is also an error).

See USB 2 specifications (5.8.3 Bulk Transfer Packet Size Constraints) for additional information.
```
An endpoint must always transmit data payloads with a data field less than or equal to the endpoint’s
reported wMaxPacketSize value. When a bulk IRP involves more data than can fit in one maximum-sized
data payload, all data payloads are required to be maximum size except for the last data payload, which will
contain the remaining data. A bulk transfer is complete when the endpoint does one of the following:

• Has transferred exactly the amount of data expected
• Transfers a packet with a payload size less than wMaxPacketSize or transfers a zero-length packet
```

### Example 1: The IRP is full

For a USB3 bulk pipe, the `wMaxPacketSize` is 1024. The sender "S" wishes
to send 2048 bytes. It creates a IRP, fills it with the 2048 bytes, and gives the IRP
to the USB controller. On the
received side "R", the USB controller is provided with a IRP of side 2048.

```
Traffic:
S -> 1024 -> R
S -> 1024 -> R IRP full, Transfer OK!
```

At this point R's IRP is full. R USB controller declares the `Transfer` over
and calls whatever callback the client provided the IRP.

### Example 2: Short-Packet

Same USB3 bulk as Example 1. The `wMaxPacketSize` is 1024. The sender wishes
to send 2148 bytes. It creates a IRP of size 2148 bytes and fills it with data.
On the received side, the USB controller is provided with a IRP of size 4096.

```
Traffic:
S -> 1024 -> R
S -> 1024 -> R
S ->  100 -> R Short-Packet, Transfer OK!
```

The receiver end detects a short packet. Even though it was provided with a 4906
byte IRP, it declares the `Transfer` completed (and records the actual size
of the `Transfer` in the IRP).

### Example 3: Overflow

Same USB3 bulk as Example 1. The `wMaxPacketSize` is 1024. The sender wishes
to send 4096 bytes. It creates a IRP, fills it with the 4096 bytes. On the
receiver side, the USB controller is provided with an IRP of size 2148.

```
Traffic:
S -> 1024 -> R
S -> 1024 -> R
S -> 1024 -> R ERROR, Transfer failed!
```

On the third packet, the receiver runs out of space in the IRP (it only had 100
bytes available). Without a way to fully store this packet,
it discards everything and returns an error stating that the `Transfer` was not successful.

## Preventing overflow and the need for Zero-Length Packets

There are two techniques to avoid overflows.

### Using a protocol
One technique is to create a protocol on top of `Transfers`.
ADB does that with its "aprotocol" ([protocol.md](protocol.md)).

In aprotocol, the sender creates a `Transfer` containing a header which is
always 24 bytes. Then it sends a separate `Transfer` containing the payload.
The size of the payload is in the header. This way the receiver always knows
what size of IRP to provide to the USB controller: it first requests a 24 byte IRP
read, extracts the size of the payload, then issues a second IRP read request
with the extracted size of the payload.

### Using a multiple of `wMaxPacketSize`

The other technique to avoid overflows is for the receiver to always use a IRP with
a size which is a multiple of the `wMaxPacketSize`. This way a `Transfer` always ends properly.
* A max size packet will exactly finish to fill the IRP, ending the `Transfer`.
* A short packet will end the `Transfer`.

This technique comes with an edge case. Take the example of a USB3 pipe where
`wMaxPacketSize` is 1024. The sender wishes to send 3072 byte. It creates a IRP
of that size, fills in the data and gives it to the USB controller which breaks
it into Packets. The receiver decides to read with a IRP of size 4096.

```
Traffic:
S -> 1024 -> R
S -> 1024 -> R
S -> 1024 -> R
.
.
.
Stalled!
```

After the USB controller on the sender side has sent all the data in the IRP, it won't send anything else.
But none of the ending conditions of a `Transfer` have been reached on the receiving end. No overflow, no short-packet, and the IRP is not
full (there is still 1024 bytes unused). As is, the USB controller on the receiving end will never declare the `Transfer`
either successful or failed. This is a stall (at least until another Packet is sent, if ever).

This condition is entered when the size of a IRP to send is a multiple of `wMaxPacketSize`
but less than the size of the IRP provided by the receiving end. To fix this condition,
the sender MUST issue a Zero-Length Packet. Technically, this is a short packet (it is less
than `wMaxPacketSize`). Upon receiving the ZLP, the receiver declares the `Transfer`
finished.

```
Traffic:
S -> 1024 -> R
S -> 1024 -> R
S -> 1024 -> R
S ->    0 -> R Short-Packet, Transfer is over!
```

## Implementation choices

By now, it should be clear that whether a sender needs to send a ZLP depends on the way
the receiver end works.

### ADB Device to Host pipe communication design

The receiver on the host leverages ADB aprotocol ([protocol.md](protocol.md)). It
first creates a IRP of size 24 bytes to receive the header. Then it creates a IRP
`Transfer`
of the size of the payload. Because the IRPs are always exactly the size of the `Transfer`
the device sends, there is no need for LZP. The USB Controller on the host side will always be able
to declare a `Transfer` complete when the IRP is full and there will never be any overflow.

The drawback of this technique is that it can consume a lot of RAM because multiple
IRPs can be in flight at a time. With the maximum size of
a apacket payload being MAX_PAYLOAD (1MiB), things can quickly add up.


### ADB Host to Device pipe communication design

On the device side, the receiver does not leverage the ADB aprotocol ([protocol.md](protocol.md)).
I suspect this was done to reduce memory consumption (the first Android device had a total RAM size of 192MiB).

The UsbFS connection always requests the same
`Transfer` size. To prevent overflows, the size is picked to be a multiple of the `wMaxPacketSize` (1x would be
valid but the overhead would kill performances). Currently, the value is kUsbReadSize (16384). USB endpoints
have a well known `wMaxPacketSize` so 16384 works for all of them (this list is for bulk transfers only which
ADB exclusively uses).

* Full Speed: 8, 16, 32, or 64 bytes.
* High Speed: 512 bytes.
* Super Speed: 1024 bytes.

When the apacket payload size is a multiple
of the `wMaxPacketSize`, the sender on the host side MUST send a ZLP to avoid stalling
on the receiver end.


## What happens if the host sender has a bug and ZLPs are not sent?

If there is a bug on the host and ZLPs are not sent, several things can happen.
You can observe normal behavior, stalled communication, or even device disconnection.

Because there are many buffers before the USB controller layer
is hit, the issue won't be deterministic. However my experience showed that attempting to
push 10GiB  rarely
fails to bring up instabilities.

```
$ dd if=/dev/urandom of=r.bin bs=1G count=10 iflag=fullblock`
$ adb push -Z r.bin /datal/local/tmp
```

### 1. Nothing breaks

You could be unlucky and not trigger the fault.

### 2. Stall

A payload of a size that's a multiple of `wMaxPacketSize` but of size less than kUsbReadSize (16384) is sent.
This is a stall as previously described.

### 3. Disconnection (due to merged packets)

In real-life usage, there is rarely a single thing happening on ADB. Users often also run logcat, Studio
monitors metrics, or perhaps the user has a shell opened. What happens if a connection goes stalls
but then something else sends an apacket?

The first `Transfer` of the apacket will be an apacket header which is 24 bytes. This will be considered
a short-packet. The previous stalled `Transfer` will be completed with the header appended. This will
confuse UsbFS since the payload will be 24 bytes more than it should be. In this condition, the connection
is closed. The log message is

```
received too many bytes while waiting for payload
```

or

```
received packet of unexpected length while reading header
```

A summary inspection of logs may make it look like a payload `Transfer`
was merged with the next header `Transfer`.