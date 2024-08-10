#ifndef _MESSAGE_QUEUE_H_
#define _MESSAGE_QUEUE_H_

#include <pthread.h>
#include <assert.h>
#include "ring_buffer.h"

/**
 * Structure which defines a common message header.
 */
typedef struct _message_header_t {
    uint32_t   magic;
    int32_t    src_id;
    int32_t    message_type;
    int32_t    message_length;
} message_header_t;

#define MSG_TYPE(m)  ((m)->message_type)
#define MSG_SIZE(m)  ((m)->message_length)

#define MSG_MAGIC   0xDEADBEEF

#define VALIDATE_MSG(m) assert((m) && ((m)->magic == MSG_MAGIC)) 
/**
 * Structure which holds a message queue.
 */
typedef struct _message_queue_t {
    int32_t         queue_id;
    int32_t         fd;
    ring_buffer_t   *message_ring;
    pthread_mutex_t write_lock;
} message_queue_t;

#define MSGQ_FD(que)    ((que)->fd)
#define MSGQ_WLOCK(que) (&((que)->write_lock))

int message_queue_init (int que_num, int msg_size); 
message_queue_t *message_queue_new (int que_id, uint32_t que_size);
int message_queue_free (message_queue_t *que);

message_header_t *message_new (int src_id, int message_type, int length);
void message_free (message_header_t *message);
int message_send (message_header_t *message, int dest_id);
message_header_t *message_recv (message_queue_t *message_que);

#endif
