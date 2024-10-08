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


#define MEM_POOL_COUNT   256

/**
 * Structure which holds a message queue.
 */
typedef struct _message_queue_t {
    int32_t              queue_id;
    int32_t              fd;
    msg_notif_cb_func_t  send_cb_funcptr;
    void *               cb_arg;
    ring_buffer_t        *message_ring;
    pthread_mutex_t      write_lock;
} message_queue_t;

#define MSGQ_ID(que)    ((que)->queue_id)
#define MSGQ_FD(que)    ((que)->fd)
#define MSGQ_WLOCK(que) (&((que)->write_lock))

static int max_queue_num;
static message_queue_t *msg_queues;
static FixedMemPool *msg_pool;
static int mem_size;
static pthread_mutex_t q_table_lock;

int message_queue_get_fd(message_queue_t *que)
{
    return MSGQ_FD(que);
}

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

    (void)!pthread_mutex_init(&q_table_lock, NULL);

    mem_size = msg_size;

    return 0;

init_err:
    if (msg_queues) {
        free(msg_queues);
    }
    return -1;
}

message_queue_t *
message_queue_new(int que_id, uint32_t que_size,
                  msg_notif_cb_func_t cb, void *arg)
{
    message_queue_t *que = NULL;

    if (que_id < max_queue_num) {
        que = &msg_queues[que_id];
        pthread_mutex_lock(&q_table_lock);
        if (!que->message_ring) {
            que->queue_id = que_id;
            que->message_ring = ring_buffer_new(que_size * sizeof(void*));
            if (!que->message_ring) {
                goto que_new_err;
            } else {
                if (!cb) {
                    que->fd = eventfd(0, EFD_NONBLOCK);
                    if (que->fd == -1) {
                        goto que_new_err;
                    } else {
                        if (pthread_mutex_init(&que->write_lock, NULL)) {
                            goto que_new_err;
                        }
                    }
                } else {
                    que->fd = -1;
                    que->send_cb_funcptr = cb;
                    que->cb_arg = arg;
                }
            }
        } else {
            /*
             * Queue allocated.
             */
            pthread_mutex_unlock(&q_table_lock);
            return NULL;
        }
    }

    pthread_mutex_unlock(&q_table_lock);
    return que;

que_new_err:
    if (que) {
        pthread_mutex_unlock(&q_table_lock);
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
        if (que->fd != -1) {
            close(que->fd);
        }
    } else {
        assert(0);
    }
    return 0;
}

message_header_t *message_new(int src_id, int message_type, int length)
{
    message_header_t *msg;
    MemPoolError rtn;

    if (length > mem_size) {
        return NULL;
    }
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
        if (MSGQ_FD(que) != -1) {
            (void)!write(MSGQ_FD(que), &num, sizeof(num));
        } else {
            que->send_cb_funcptr(que, que->cb_arg);
        }
    }

    return rtn;
}

int message_recv(message_queue_t *que, msg_handler_cb_func_t rcv_cb, void *arg)
{
    message_header_t *m = NULL;
    uint64_t num;
    int i = 0;

    if (que && que->message_ring) {
        if (que->send_cb_funcptr) {
            while ((m = ring_buffer_deq(que->message_ring))) {
                rcv_cb(que, m, arg);
                i++;
            }
        } else {
            (void)!read(MSGQ_FD(que), &num, sizeof(num));
            while(num--) {
                m = (message_header_t *)ring_buffer_deq(que->message_ring);
                assert(m);
                rcv_cb(que, m, arg);
                i++;
            }
        }
    }
    return i;
}
