#ifndef _MESSAGE_QUEUE_H_
#define _MESSAGE_QUEUE_H_

#include <pthread.h>
#include <assert.h>
#include "ring_buffer.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

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
#define MSG_SRC(m)   ((m)->src_id)

#define MSG_MAGIC   0xDEADBEEF

#define VALIDATE_MSG(m) assert((m) && ((m)->magic == MSG_MAGIC)) 

typedef struct _message_queue_t message_queue_t;

/**
 * Subsys Initialization
 * Params
 *     int :  Max queue number. This number specifies the
 *            range of queue-id.
 *     int :  Max message size
 * Return
 *     int :  0 for success; -1 for failure
 */
int message_queue_init (int, int);

/**
 * Return the associcated eventfd of a message queue
 * Params
 *     message_queue_t * :  pointer to a message queue
 * Return:
 *     int               :  eventfd handle.
 */
int message_queue_get_fd(message_queue_t *);


typedef void (*msg_notif_cb_func_t)(message_queue_t *, void *arg);
/**
 * Create a new message queue
 * Params
 *     int      :  id to identify this queue.
 *     uint32_t :  queue depth.
 *     msg_notif_cb_func_t :
 *                 An external notification callback provided by caller.
 *                 If this parameter is set to NULL, message_queue_new
 *                 allocates a new eventfd for message notification.
 *                 Otherwise, the callback provided by caller
 *                 is responsible for the entire notification.
 *     void *      argument to be passed to external callback.
 * Return
 *     message_queue_t *
 *              :  Pointer to the new queue or NULL on any failure.
 */
message_queue_t *
message_queue_new (int, uint32_t, msg_notif_cb_func_t, void *);

/**
 * Destroy a message queue
 * Params
 *     message_queue_t *
 *              : The queue to be destroyed.
 * Return
 *     int      : always 0
 */
int message_queue_free (message_queue_t *);

/**
 * Create a new message.
 * Params
 *     int      :  source queue(module) id.
 *     int      :  message type
 *     int      :  message length.
 * Return
 *     message_header_t * :
 *              pointer to the new message or NULL on any failure.
 */
message_header_t *message_new (int, int, int);

/**
 * Free a message.
 * Params
 *     message_header_t * : message to be freed.
 * Return
 *     None
 */
void message_free (message_header_t *);

/**
 * Send a message to destiantion queue.
 * Params
 *     message_header_t *   : Message to be sent
 *     int                  : Destination queue(module) ID
 * Return
 *     int                  :  0 for success;
 *                            -1 for any failure
 */
int message_send (message_header_t *, int);

typedef void (*msg_handler_cb_func_t)(message_queue_t *, \
                                      message_header_t *, void *arg);
/**
 * Retrieve all messages in a queue. Callback function is invoked
 * for each message.
 * Params
 *     message_queue_t *     : message queue
 *     msg_handler_cb_func_t : callback provided by caller.
 *     void *                : param to be passed to callback above.
 * Return
 *     int                   : Number of the messages processed.
 */
int message_recv (message_queue_t *, msg_handler_cb_func_t, void *);

#endif
