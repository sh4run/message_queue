/*
 * Copyright (c) 2024  sh4run
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of message_queue nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include "message_queue.h"
#include "mem_pool.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define MEM_POOL_COUNT   256

static int max_queue_num;
static message_queue_t *msg_queues;
static FixedMemPool *msg_pool;

int message_queue_init(int que_num, int msg_size)
{
    max_queue_num = que_num;
    msg_queues = (message_queue_t*)malloc(sizeof(message_queue_t) * que_num);
    if (!msg_queues) {
        goto init_err;
    }
    memset(msg_queues, 0, sizeof(message_queue_t) * que_num);

    if (pool_fixed_init(&msg_pool, msg_size, MEM_POOL_COUNT) 
                                            != MEM_POOL_ERR_OK) {
        goto init_err;
    }

    return 0;

init_err:
    if (msg_queues) {
        free(msg_queues);
    }
    return -1;
}

message_queue_t *message_queue_new(int que_id, uint32_t que_size)
{
    message_queue_t *que = NULL;

    if (que_id < max_queue_num) {
        que = &msg_queues[que_id];
        if (!que->message_ring) {
            que->queue_id = que_id;
            que->message_ring = ring_buffer_new(que_size * sizeof(void*));
            if (!que->message_ring) {
                goto que_new_err;
            } else {
                que->fd = eventfd(0, EFD_NONBLOCK);
                if (que->fd == -1) {
                    goto que_new_err;
                } else {
                    if (pthread_mutex_init(&que->write_lock, NULL)) {
                        goto que_new_err;
                    }
                }
            }
        }
    }

    return que;

que_new_err:
    if (que) {
        if (que->fd != -1) {
            close(que->fd);
        }
        if (que->message_ring) {
            ring_buffer_free(que->message_ring);
            que->message_ring = NULL;
        }
    }
    return NULL;
}

int message_queue_free(message_queue_t *que)
{
    if (que && que->message_ring) {
        ring_buffer_free(que->message_ring);
        que->message_ring = NULL;
        pthread_mutex_destroy(&que->write_lock);
        close(que->fd);
    } else {
        assert(0);
    }
    return 0;
}

message_header_t *message_new(int src_id, int message_type, int length)
{
    message_header_t *msg;
    MemPoolError rtn;

    rtn = pool_fixed_alloc(msg_pool, (void**)&msg);
    if (rtn != MEM_POOL_ERR_OK) {
        return NULL;
    }

    msg->magic = MSG_MAGIC;
    msg->src_id = src_id;
    msg->message_type = message_type;
    msg->message_length = length;

    return msg;
}

void message_free(message_header_t *message)
{
    VALIDATE_MSG(message);

    message->magic = 0;
    pool_fixed_free(msg_pool, message);
}

/**
 * Send a message.
 * Return:
 *     0 : Success
 *    -1 : Destination queue is full.
 */
int message_send(message_header_t *message, int dest_id)
{
    message_queue_t *que;
    int rtn;
    
    VALIDATE_MSG(message);

    assert(dest_id < max_queue_num);

    que = &msg_queues[dest_id];

    /*
     * Use a write protection in case of multiple producers.
     */
    pthread_mutex_lock(MSGQ_WLOCK(que));
    rtn = ring_buffer_enq(que->message_ring, (void*)message);
    pthread_mutex_unlock(MSGQ_WLOCK(que));

    if (!rtn) {
        uint64_t num = 1;
        (void)!write(MSGQ_FD(que), &num, sizeof(num));
    }

    return rtn;
}

message_header_t *message_recv(message_queue_t *que)
{
    message_header_t *m = NULL;
    if (que && que->message_ring) {
        m = (message_header_t *)ring_buffer_deq(que->message_ring);
    }
    return m;
}
