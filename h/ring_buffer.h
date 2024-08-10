/*
 * This implementation is derived from:
 *     https://github.com/AndersKaloer/Ring-Buffer/blob/master/ringbuffer.c
 */

#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

#include <stdlib.h>

/**
 * Structure which holds a ring buffer.
 */
typedef struct _ring_buffer_t {
    /*  mask. */
    uint32_t mask;
    /* Index of tail(deq). */
    uint32_t tail;
    /* Index of head(enq). */
    uint32_t head;
    /* Buffer memory. */
    void  *buffer[0];
} ring_buffer_t;


/**
 * Returns whether a ring buffer is empty.
 * @param buffer The buffer for which it should be returned whether it is empty.
 * @return 1 if empty; 0 otherwise.
 */
static inline int ring_buffer_is_empty(ring_buffer_t *ring) {
    return (ring->head == ring->tail);
}

/**
 * Returns whether a ring buffer is full.
 * @return 1 if full; 0 otherwise.
 */
static inline int ring_buffer_is_full(ring_buffer_t *ring) {
    return (((ring->head - ring->tail) & ring->mask) == ring->mask);
}

/**
 * Initialze a new ring. 
 * Ring size must be power of 2.
 */
static inline ring_buffer_t *ring_buffer_new(uint32_t size)
{
    ring_buffer_t *ring;
    if (size & (size-1)) {
        /* ring size must be power of 2 */
        return NULL;
    }
    ring = (ring_buffer_t *)malloc(sizeof(ring_buffer_t) + size * sizeof(void*));
    if (ring) {
        ring->mask = size - 1;
        ring->tail = ring->head = 0;
    }
    return ring;
}

/**
 * Free a ring.
 */
static inline void ring_buffer_free(ring_buffer_t *ring)
{
    ring->head = ring->tail = ring->mask = 0;
    free(ring);   
}

/**
 * Adds an element to a ring buffer.
 */
static inline int ring_buffer_enq(ring_buffer_t *ring, void *data)
{
    if (ring_buffer_is_full(ring)) {
        return -1;
    }

    ring->buffer[ring->head] = data;
    ring->head = ((ring->head + 1) & ring->mask);
    return 0;
}

/**
 * Returns the oldest element in a ring buffer.
 */
static inline void *ring_buffer_deq(ring_buffer_t *ring)
{
    void *elem;
    if (ring_buffer_is_empty(ring)) {
        return NULL;
    }

    elem = ring->buffer[ring->tail];
    ring->tail = ((ring->tail + 1) & ring->mask);
    return elem;
}

/**
 * Returns the number of items in a ring buffer.
 * @return The number of items in the ring buffer.
 */
static inline uint32_t ring_buffer_num_items(ring_buffer_t *ring)
{
    return ((ring->head - ring->tail) & ring->mask);
}

#endif 
