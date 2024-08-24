#include <stdlib.h>                                                                             
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <ev.h>

#include "message_queue.h"

typedef enum _module_t {
    mod_0 = 0,
    mod_1,
    mod_2,
    mod_max
} module_t;

enum MESSAGE_TYPE {
    message_type_1,
    message_type_2,
    message_type_3
};

typedef struct _message_1_t {
    message_header_t    header;
    uint32_t            ttl;
    uint32_t            pad1;
    uint32_t            pad2;
} message_1_t;

typedef struct _message_2_t {
    message_header_t    header;
    uint32_t            pad2;
    uint32_t            pad1;
    uint32_t            ttl;
} message_2_t;

#define PAD1_MAGIC   0x3377aabb
#define PAD2_MAGIC   0x5678efcd

typedef struct _msg_ev_ctx_t {
    struct ev_loop  *loop;
    module_t        mid;
    message_queue_t *msg_que;
    ev_io           msgq_watcher;
    ev_async        q_recv_watcher;
    ev_signal       signal_watcher;
    uint32_t        ttl;
    uint64_t        msg_1_rcv;
    uint64_t        msg_1_sent;
    uint64_t        msg_1_create;
    uint64_t        msg_1_free;
    uint64_t        msg_1_fail;
    uint64_t        msg_2_rcv;
    uint64_t        msg_2_sent;
    uint64_t        msg_2_create;
    uint64_t        msg_2_free;
    uint64_t        msg_2_fail;
} msg_ev_ctx_t;


static void 
msg_handler_1(message_header_t *header, msg_ev_ctx_t *ctx)
{
    int dest;
    message_1_t *msg = (message_1_t*)header;

    ctx->msg_1_rcv++;
    assert(msg->pad1 == PAD1_MAGIC && msg->pad2 == PAD2_MAGIC);

    if (!(--msg->ttl)) {
        ctx->msg_1_free++;
        message_free(header);
        /* create and send a new copy out */
        msg = (message_1_t *)message_new(ctx->mid,
                                         message_type_1, sizeof(message_1_t));
        if (msg) {
            msg->ttl = ((++ctx->ttl) & 0x3f) + 1;
            msg->pad1 = PAD1_MAGIC;
            msg->pad2 = PAD2_MAGIC;
            dest = ctx->mid + 1;
            dest = dest == mod_max ? mod_0 : dest;
            if (!message_send(&msg->header, dest)) {
                ctx->msg_1_create++;
            } else {
                ctx->msg_1_fail++;
            }
        } else {
            ctx->msg_1_fail++;
        }
    } else {
        msg->header.src_id = ctx->mid;
        dest = ctx->mid + 1;
        dest = dest == mod_max ? mod_0 : dest;
        if (!message_send(&(msg->header), dest)) {
            ctx->msg_1_sent++;
        } else {
            ctx->msg_1_fail++;
        }
    }
}

static void
msg_handler_2(message_header_t *header, msg_ev_ctx_t *ctx)
{
    int dest;
    message_2_t *msg = (message_2_t*)header;

    ctx->msg_2_rcv++;
    assert(msg->pad1 == PAD1_MAGIC && msg->pad2 == PAD2_MAGIC);

    if (!(--msg->ttl)) {
        ctx->msg_2_free++;
        message_free(header);
        msg = (message_2_t *)message_new(ctx->mid, 
                                         message_type_2, sizeof(message_2_t));
        if (msg) {
            msg->ttl = ((++ctx->ttl) & 0x3f) + 1;
            msg->pad1 = PAD1_MAGIC;
            msg->pad2 = PAD2_MAGIC;            
            dest = ctx->mid - 1;
            dest = dest < mod_0 ? mod_2 : dest;
            if (!message_send(&msg->header, dest)) {
                ctx->msg_2_create++;
            } else {
                ctx->msg_2_fail++;
            }
        } else {
            ctx->msg_2_fail++;
        }
    } else {
        msg->header.src_id = ctx->mid;
        dest = ctx->mid - 1;
        dest = dest < mod_0 ? mod_2 : dest;
        if (!message_send(&(msg->header), dest)) {
            ctx->msg_2_sent++;
        } else {
            ctx->msg_2_fail++;
        }
    }
}

static void
msg_handler_3(message_header_t *header, msg_ev_ctx_t *ctx)
{
    message_free(header);

    printf("queue %d stats:\n", ctx->mid);
    printf("   msg_1 rcv %ld, sent %ld, create %ld, free %ld, fail %ld\n",
           ctx->msg_1_rcv, ctx->msg_1_sent, ctx->msg_1_create, 
           ctx->msg_1_free, ctx->msg_1_fail);
    printf("   msg_2 rcv %ld, sent %ld, create %ld, free %ld, fail %ld\n",
           ctx->msg_2_rcv, ctx->msg_2_sent, ctx->msg_2_create,
           ctx->msg_2_free, ctx->msg_2_fail);
    ev_break(ctx->loop, EVBREAK_ALL);
}

static void 
msg_handler(message_queue_t *que, message_header_t *header, void *arg)
{
    UNUSED(que);
    msg_ev_ctx_t *ctx = (msg_ev_ctx_t *)arg;

    switch (MSG_TYPE(header)) {
        case message_type_1 :
            msg_handler_1(header, ctx);
            break;
        case message_type_2 :
            msg_handler_2(header, ctx);
            break;
        case message_type_3 :
            msg_handler_3(header, ctx);
            break;
        default :
            fprintf(stderr, "%s: unsupported type %d\n", 
                    __func__, MSG_TYPE(header));
            break;
    }
}

static void 
msgq_read_ev_cb(struct ev_loop *loop, ev_io *w, int revents)
{
    UNUSED(revents);
    UNUSED(w);

    msg_ev_ctx_t *ctx = (msg_ev_ctx_t*)ev_userdata(loop);

    assert(ctx);

    message_recv(ctx->msg_que, msg_handler, ctx);
}

static void
sigint_cb (struct ev_loop *loop, ev_signal *w, int revents)
{
    UNUSED(revents);
    UNUSED(w);

    msg_ev_ctx_t *ctx;

    ctx = (msg_ev_ctx_t*)ev_userdata(loop);
    assert(ctx);

    printf("\nqueue %d stats:\n", ctx->mid);
    printf("   msg_1 rcv %ld, sent %ld, create %ld, free %ld, fail %ld\n",
           ctx->msg_1_rcv, ctx->msg_1_sent, ctx->msg_1_create,
           ctx->msg_1_free, ctx->msg_1_fail);
    printf("   msg_2 rcv %ld, sent %ld, create %ld, free %ld, fail %ld\n",
           ctx->msg_2_rcv, ctx->msg_2_sent, ctx->msg_2_create,
           ctx->msg_2_free, ctx->msg_2_fail);

    /* send msg_3 to children threads to exit */
    message_header_t *h = message_new(mod_0, message_type_3, 
                                      sizeof(message_header_t));
    if (h) {
        message_send(h, mod_1);
    }
    h = message_new(mod_0, message_type_3,
                    sizeof(message_header_t));
    if (h) {
        message_send(h, mod_2);
    }

    ev_break (loop, EVBREAK_ALL);
}

static void
q_recv_cb (struct ev_loop *loop, ev_async *w, int revents)
{
    UNUSED(w);
    UNUSED(revents);

    msg_ev_ctx_t *ctx;
    ctx = (msg_ev_ctx_t*)ev_userdata(loop);
    assert(ctx);

    message_recv(ctx->msg_que, msg_handler, ctx);
}

static volatile int child_thread_ready;

static void* 
child_thread(void* args)
{
    msg_ev_ctx_t ctx;
    int id = *((int*)args);
    int i;
    message_header_t *h;

    memset(&ctx, 0, sizeof(msg_ev_ctx_t));
    ctx.mid = id;
    /*
     * Create a message queue for a child thread. This message
     * queue uses the embedded eventfd for message notification. 
     * An io-watcher has to be added into the ev-loop to monitor
     * this eventfd.
     */
    ctx.msg_que = message_queue_new(ctx.mid, 512, NULL, NULL);
    if (!ctx.msg_que) {
        fprintf(stderr, 
                "Fail to create child(%d) message queue\n", id);
        return NULL;
    }

    ctx.loop = ev_loop_new(EVFLAG_AUTO);
    ev_set_userdata(ctx.loop, (void*)&ctx);

    /*
     * Create a io-watcher to monitor the eventfd of the
     * message queue.
     */
    ev_io_init(&ctx.msgq_watcher, msgq_read_ev_cb,
               message_queue_get_fd(ctx.msg_que), EV_READ);
    ev_io_start(ctx.loop, &ctx.msgq_watcher);

    /* Mark this thread is ready to receive messages. */
    child_thread_ready++;

    for (i = 0; i < 100; i++) {
        h = NULL;
        if (ctx.mid == mod_2) {
            message_1_t *m1 = (message_1_t*)message_new(ctx.mid, message_type_1, 
                                                        sizeof(message_1_t));
            if (m1) {
                m1->ttl = (++ctx.ttl & 0x3f) + 1;
                m1->pad1 = PAD1_MAGIC;
                m1->pad2 = PAD2_MAGIC;                
                h = &(m1->header);
                ctx.msg_1_create++;
            }
        } else {
            message_2_t *m2 = (message_2_t*)message_new(ctx.mid, message_type_2, 
                                                        sizeof(message_2_t));
            if (m2) {
                m2->ttl = (++ctx.ttl & 0x3f) + 1;
                m2->pad1 = PAD1_MAGIC;
                m2->pad2 = PAD2_MAGIC;                
                h = &(m2->header);
                ctx.msg_2_create++;
            }                                          
        }
        if (h) {
            message_send(h, mod_0);
        }
    }

    ev_run(ctx.loop, 0);

    return NULL;
}

static void 
msg_rcv_notif_cb(message_queue_t *que, void *arg)
{
    UNUSED(que);
    msg_ev_ctx_t *ctx = (msg_ev_ctx_t*)arg;
    ev_async_send(ctx->loop, &ctx->q_recv_watcher);
}

int main(void)
{
    pthread_t t1, t2;
    msg_ev_ctx_t  msg_ctx_t0;
    int num1 = mod_1, num2 = mod_2; 
    struct ev_loop *main_loop;

    memset(&msg_ctx_t0, 0, sizeof(msg_ev_ctx_t));
    msg_ctx_t0.mid = mod_0;

    main_loop = ev_default_loop(0);
    msg_ctx_t0.loop = main_loop;
    ev_set_userdata(main_loop, (void*)&msg_ctx_t0);

    ev_async_init(&msg_ctx_t0.q_recv_watcher, q_recv_cb);
    ev_async_start(main_loop, &msg_ctx_t0.q_recv_watcher);

    /* 
     * Initialize message queue with:
     *       Max 3 (mod_max) queues;
     *       Chunk size of memory pool 64 (max message size)
     */
    message_queue_init(mod_max, 64);

    /*
     * Create a message queue with:
     *       queue-id equal to mod_0;
     *       queue depth equal to 512, i.e. buffer up to 512 messages;
     *       not using the embedded eventfd, but the external 
     *       msg_rcv_notif_cb for message notification. When a message
     *       is sent to this queue, the callback is invoked. 
     *       msg_rcv_notif_cb calls ev_async_send to notify the ev-loop
     *       of this thread. 
     */
    msg_ctx_t0.msg_que = message_queue_new(msg_ctx_t0.mid, 512, 
                               msg_rcv_notif_cb, &msg_ctx_t0);
    if (!msg_ctx_t0.msg_que) {
        fprintf(stderr, "Fail to create message queue\n");
        return -1;
    }

    ev_signal_init(&msg_ctx_t0.signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(main_loop, &msg_ctx_t0.signal_watcher);

    pthread_create(&t1, NULL, child_thread, (void*)&num1);
    pthread_create(&t2, NULL, child_thread, (void*)&num2);

    /*
     * Waiting for children threads to complete its initialization
     */
    while (child_thread_ready < mod_max -1) {
        sched_yield();
    }

    printf("Press CTRL-C to exit. \n");
    ev_run(main_loop, 0);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
