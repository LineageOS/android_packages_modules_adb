# Understanding asockets

The data structure of asocket, with their queue, amessage, and apackets are described
in [internals.md](internals.md). But understanding asocket, how they are used, and how they are
paired is non-trivial. This document hopefully explains how bytes flow through them.

## Why ADB needs asocket

The concept of `asocket` was created to achieve two things.

- Carry multiple streams over a single pipe (originally that meant USB only).
- Manage congestion (propagate back-pressure).

With the introduction of TCP support, an abstraction layer (transport) was created
but TCP multiplexing was not leveraged. Even when using TCP, a transport still uses `asocket`
to multiplex streams.

## Data direction and asocket peers

- A asocket is uni-directional. It only allows data to be `enqueue`d.
- A asocket is paired with a peer asocket which handles traffic in the opposite direction.

## Types of asocket

There are several types of `asocket`. Some are easy to understand because they
extend `asocket`.

- JdwpSocket
- JdwpTracker
- SinkSocket
- SourceSocket

However there are "undeclared" types, whose behavior differs only via the `asocket`
function pointers.

- Local Socket (LS)
- Remote Socket (RS)
- Smart Socket (SS)
- Local Service Socket (LSS)


## Local socket (abbreviated LS)

A LS interfaces with a file descriptor to forward a stream of bytes
without altering it (as opposed to a LSS).
To perform its task, a LS leverages fdevent to request FDE_READ/FDE_WRITE notification.

```
                                  LOCAL SOCKET                                TRANSPORT
                   ┌────────────────────────────────────────────────┐           ┌──┐
     ┌──┐ write(3) │  ┌─────┐                                   enqueue()       │  │
     │  │◄─────────┼──┤Queue├─────────────◄──────────────◄──────────┼─────────(A_WRTE)◄──
     │fd│          │  └─────┘                                       │           │  │
     │  ├──────────►─────────────────┐                              │           │  │
     └──┘ read(3)  └─────────────────┼──────────────────────────────┘           │  │
                                     ▼                                          └──┘
                                 peer.enqueue()
```

A_WRTE apackets are forwarded directly to the LS by the transport. The transport
is able to route the apacket to the local asocket by using `apacket.msg1` which
points to the target local asocket `id`.

### Write to fd and Back-pressure

When a payload is enqueued, an LS tries to write as much as possible to its `fd`.
After the write attempt, the LS stores in its queue what could not be written.
Based on the volume of data in the queue, it sets `FDE_WRITE` and allows/forbids
more data to come.

- If there is data in the queue, the LS always requests `FDE_WRITE` events so it
can write the outstanding data.
- If there is less than `MAX_PAYLOAD` in the queue, LS calls ready on its peer (a RS),
so an A_OKAY apacket is sent (which trigger another A_WRTE packet to be send).
- If there is more than `MAX_PAYLOAD` in the queue, back-pressure is propagated by not
calling `peer->ready`. This will trigger the other side to not send more A_WRTE until
the volume of data in the queue has decreased.

### Read from fd and Back-pressure

When it is created, a LS requests FDE_READ from fdevent. When it triggers, it reads
as much as possible from the `fd` (within MAX_PAYLOAD to make sure transport will take it).
The data is then enqueueed on the peer.

If `peer.enqueue` indicates that the peer cannot take more updates, the LS deletes
the FDE_READ request.
It is re-installed when A_OKAY is received by transport.

## Remote socket (abbreviated RS)

A RS handles outbound traffic and interfaces with a transport. It is simple
compared to a LS since it merely translates function calls into transport packets.

- enqueue -> A_WRTE
- ready   -> A_OKAY
- close   -> A_CLSE on RS and peer.
- shutdown-> A_CLSE

A RS is often paired with a LS  or a LSS.
```
                                    LOCAL SOCKET (THIS)                      TRANSPORT    
                   ┌────────────────────────────────────────────────┐           ┌──┐      
     ┌──┐ write(3) │  ┌─────┐                      enqueue()        │           │  │      
     │  │◄─────────┼──┤Queue├─────────────◄──────────────◄──────────┼─────────(A_WRTE)◄── 
     │fd│          │  └─────┘                                       │           │  │      
     │  ├──────────►─────────────────┐                              │        ─  │  │      
     └──┘ read(3)  └─────────────────┼──────────────────────────────┘           │  │      
                                     │                                          │  │      
                   ┌─────────────────▼─────────────────▲────────────┐           │  │      
                   │                 │                              │           │  │      
                   │                 │                              │           │  │      
                   │                 └─────────────────────►──────────────────(A_WRTE)───►
                   │                enqueue()                       │           │  │      
                   └────────────────────────────────────────────────┘           └──┘      
                                    REMOTE SOCKET (PEER)                                  
```

### RS creation

A RS is always created by the transport layer (on A_OKAY or A_OPEN) and paired with a LS or LSS. 

- Upon A_OPEN: The transport creates a LSS to handle inbound traffic and peers it with
a RS to handle outbound traffic.

- Upon A_OKAY: When receiving this packet, the transport always checks if there is a 
LS with the id matching `msg1`. If there is and it does not have a peer yet, a RS is
created, which completes a bi-directional chain.

## Local Service Socket (LSS)

A LSS is a wrapper around a `fd` (which is used to build a LS). The purpose is to process
inbound and outbound traffic when it needs modification. e.g.: The "framebuffer" service
involves invoking executable `screencap` and generating a header describing the payload
before forwarding the color payload. This could not be done with a "simple" LS.

The `fd` created by the LSS is often a pipe backed by a thread. 

## Smart Socket (abbreviated SS)

These Smart sockets are only created on the host by adb server on accept(3) by the listener
service. They interface with a TCP socket.

Upon creation, a SS enqueue does not forward anything until the [smart protocol](services.md) 
has provided a target device and a service to invoke. 

When these two conditions are met, the SS selects a transport and A_OPEN the service
on the device. It gives the TCP socket fd to a LS and creates a RS to build a data flow
similar to what was described in the Local Socket section.

## Examples of dataflow

### Package Manager (Device service)

Let's take the example of the command `adb install -S <SIZE> -`. There are several install
strategies but for the sake of simplicity, let's focus on the one resulting in invoking
`pm install -S <SIZE> -` on the device and then streaming the content of the APK.

In the beginning there is only a listener service, waiting for `connect(3)` on the server.

```
      ADB Client                   ADB Server       TRANSPORT        ADBd
┌──────────────────────┐      ┌─────────────────┐      │     ┌─────────────────┐
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                     tcp * ───►* alistener     │      │     │                 │
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
└──────────────────────┘      └─────────────────┘      │     └─────────────────┘
                                                                               
┌──────────┐   ┌───────┐
│    APK   │   │Console│
└──────────┘   └───────┘
```

Upon `accept(3)`, the listener service creates a SS and gives it the socket `fd`.
Then the client starts writing to the socket `|host:transport:XXXXXXX| |exec:pm pm install -S <SIZE> ->|`.

```
      ADB Client                   ADB Server       TRANSPORT        ADBd        
┌──────────────────────┐      ┌─────────────────┐      │     ┌─────────────────┐
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                     tcp * ───►* SS            │      │     │                 │
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
└──────────────────────┘      └─────────────────┘      │     └─────────────────┘
                                                                               
┌──────────┐   ┌───────┐
│    APK   │   │Console│
└──────────┘   └───────┘
```

The SS buffers the smart protocol requests until it has everything it needs from
the client.
The first part, `host:transport:XXXXXXX` lets the SS know which transport to use (it
contains the device identified `XXXXXXX`). The second part is the service to execute
`exec:pm pm install -S <SIZE> -`.

When it has both, the SS creates a LS to handle the TCP `fd`, and creates a RS to let
the LS talk to the transport. The last thing the SS does before replacing itself with a
LS (and giving it its socket fd) is sending an A_OPEN apacket.

```
      ADB Client                   ADB Server       TRANSPORT        ADBd
┌──────────────────────┐      ┌─────────────────┐      │     ┌─────────────────┐
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                      │      │                 │      │     │                 │
│                  tcp * ◄───►* LS              │      │     │                 │
│                      │      │  │              │      │     │                 │
│                      │      │  └─────────► RS─┼──────┼─►───┼──────►A_OPEN    │
└──────────────────────┘      └─────────────────┘      │     └─────────────────┘
                                                                               
┌──────────┐   ┌───────┐                                       
│   APK    │   │Console│                                      
└──────────┘   └───────┘
```
So far only one side of the pipeline has been set up.

Upon reception of the A_OPEN on the device side, `pm` is invoked via `fork/exec`.
A socket pair end is given to a LS. A RS is also created to handle bytes generated by `pm`.
Now we have a full pipeline able to handle bidirectional streams.
 
```
      ADB Client                   ADB Server       TRANSPORT        ADBd
┌──────────────────────┐      ┌─────────────────┐      │     ┌─────────────────┐
│                      │      │                 │      │     │                 │
│                      │      │  ┌──────────────┼──◄───┼─────┼─RS ◄───┐        │
│                      │      │  ▼              │      │     │        │        │
│     ┌───────────►tcp * ◄───►* LS              │      │     │        │        │
│     │             │  │      │  │              │      │     │        │        │
│     │             │  │      │  └─────────► RS─┼──────┼─►───┼──────►LS        │
└─────┼─────────────┼──┘      └─────────────────┘      │     └────────▲────────┘
      │             │                                                 │         
┌─────┴────┐   ┌────▼──┐                                        ┌─────▼────┐    
│    APK   │   │Console│                                        │    PM    │    
└──────────┘   └───────┘                                        └──────────┘    
```

At this point the client can `write(3)` the content of the apk to feed it to `pm`.
It is also able to `read(3)` to show the output of `pm` in the console.

