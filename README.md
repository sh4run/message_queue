# message_queue
An inter-thread message queue implementation based on circular buffer and eventfd.

Example code is based on libev. It creates 3 threads and illustrates how to send/receive messages using ev-loop.

## Build

    git clone https://github.com/sh4run/message_queue.git
    cd message_queue
    git submodule update --init
    make
    make example
