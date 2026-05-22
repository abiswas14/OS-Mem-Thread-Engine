#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#define RT_MAX_PAYLOAD 16
#define RT_QUEUE_CAPACITY 64

typedef enum {
    RT_DATA = 0,
    RT_LAST = 1,
    RT_ACK = 2,
    RT_NACK = 3,
    RT_STOP = 4
} RtPacketType;

typedef struct {
    RtPacketType type;
    unsigned checksum;
    size_t len;
    char payload[RT_MAX_PAYLOAD];
} RtPacket;

typedef struct {
    RtPacket items[RT_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} RtQueue;

typedef struct {
    RtQueue outbound;
    RtQueue inbound;
    pthread_t sender_thread;
    pthread_t receiver_thread;
    pthread_mutex_t ack_mu;
    pthread_cond_t ack_ready;
    RtPacketType last_ack;
    bool ack_available;
    bool running;
    double corruption_rate;
} RtConnection;

unsigned rt_checksum(const char *data, size_t len);
size_t rt_packetize(const char *message, RtPacket **out_packets);
void rt_queue_init(RtQueue *q);
void rt_queue_close(RtQueue *q);
bool rt_queue_push(RtQueue *q, const RtPacket *pkt);
bool rt_queue_pop(RtQueue *q, RtPacket *out);
int rt_connection_start(RtConnection *conn, double corruption_rate);
void rt_connection_stop(RtConnection *conn);
void rt_send_message(RtConnection *conn, const char *message);
void rt_run_demo(void);

#endif
