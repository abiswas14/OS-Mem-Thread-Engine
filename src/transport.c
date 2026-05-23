#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double pseudo_random_unit(void) {
    return (double)rand() / (double)RAND_MAX;
}

unsigned rt_checksum(const char *data, size_t len) {
    unsigned total = 0;
    for (size_t i = 0; i < len; i += 2) {
        unsigned char first = (unsigned char)data[i];
        unsigned char second = (i + 1 < len) ? (unsigned char)data[i + 1] : 0;
        total += second * (unsigned)(i);
        total += first * (unsigned)(i + 1);
    }
    return total;
}

size_t rt_packetize(const char *message, RtPacket **out_packets) {
    size_t len = strlen(message);
    size_t count = len / RT_MAX_PAYLOAD + ((len % RT_MAX_PAYLOAD) ? 1 : 0);
    if (count == 0) {
        count = 1;
    }

    RtPacket *packets = calloc(count, sizeof(RtPacket));
    if (!packets) {
        *out_packets = NULL;
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        size_t start = i * RT_MAX_PAYLOAD;
        size_t remaining = (start < len) ? len - start : 0;
        size_t chunk = remaining > RT_MAX_PAYLOAD ? RT_MAX_PAYLOAD : remaining;
        packets[i].type = (i + 1 == count) ? RT_LAST : RT_DATA;
        packets[i].len = chunk;
        if (chunk > 0) {
            memcpy(packets[i].payload, message + start, chunk);
        }
        packets[i].checksum = rt_checksum(packets[i].payload, packets[i].len);
    }

    *out_packets = packets;
    return count;
}

void rt_queue_init(RtQueue *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void rt_queue_close(RtQueue *q) {
    pthread_mutex_lock(&q->mu);
    q->closed = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);
}

bool rt_queue_push(RtQueue *q, const RtPacket *pkt) {
    pthread_mutex_lock(&q->mu);
    while (!q->closed && q->count == RT_QUEUE_CAPACITY) {
        pthread_cond_wait(&q->not_full, &q->mu);
    }
    if (q->closed) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    q->items[q->tail] = *pkt;
    q->tail = (q->tail + 1) % RT_QUEUE_CAPACITY;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return true;
}

bool rt_queue_pop(RtQueue *q, RtPacket *out) {
    pthread_mutex_lock(&q->mu);
    while (!q->closed && q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mu);
    }
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % RT_QUEUE_CAPACITY;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return true;
}

static void set_ack(RtConnection *conn, RtPacketType ack) {
    pthread_mutex_lock(&conn->ack_mu);
    conn->last_ack = ack;
    conn->ack_available = true;
    pthread_cond_signal(&conn->ack_ready);
    pthread_mutex_unlock(&conn->ack_mu);
}

static RtPacketType wait_ack(RtConnection *conn) {
    pthread_mutex_lock(&conn->ack_mu);
    while (conn->running && !conn->ack_available) {
        pthread_cond_wait(&conn->ack_ready, &conn->ack_mu);
    }
    RtPacketType ack = conn->last_ack;
    conn->ack_available = false;
    pthread_mutex_unlock(&conn->ack_mu);
    return ack;
}

static void maybe_corrupt(RtConnection *conn, RtPacket *pkt) {
    if (pkt->type == RT_DATA || pkt->type == RT_LAST) {
        if (pseudo_random_unit() < conn->corruption_rate) {
            pkt->checksum += 7;
        }
    }
}

static void *sender_main(void *arg) {
    RtConnection *conn = (RtConnection *)arg;
    RtPacket pkt;

    while (rt_queue_pop(&conn->outbound, &pkt)) {
        if (pkt.type == RT_STOP) {
            break;
        }

        bool delivered = false;
        while (conn->running && !delivered) {
            RtPacket network_packet = pkt;
            maybe_corrupt(conn, &network_packet);

            unsigned expected = rt_checksum(network_packet.payload, network_packet.len);
            RtPacketType ack = (expected == network_packet.checksum) ? RT_ACK : RT_NACK;
            set_ack(conn, ack);

            RtPacketType observed = wait_ack(conn);
            if (observed == RT_ACK) {
                delivered = true;
            } else {
                printf("sender: retransmitting corrupted packet\n");
            }
        }
    }
    return NULL;
}

static void *receiver_main(void *arg) {
    RtConnection *conn = (RtConnection *)arg;
    char assembled[512];
    size_t assembled_len = 0;

    while (conn->running) {
        pthread_mutex_lock(&conn->ack_mu);
        while (conn->running && !conn->ack_available) {
            pthread_cond_wait(&conn->ack_ready, &conn->ack_mu);
        }
        if (!conn->running) {
            pthread_mutex_unlock(&conn->ack_mu);
            break;
        }
        RtPacketType ack = conn->last_ack;
        pthread_mutex_unlock(&conn->ack_mu);

        if (ack == RT_ACK) {
            /* In a real transport stack, the receiver would append the DATA payload.
               This demo focuses on the synchronization/retransmission mechanics. */
            assembled_len += 1;
        }
    }

    printf("receiver: observed %zu successfully acknowledged packet events\n", assembled_len);
    (void)assembled;
    return NULL;
}

int rt_connection_start(RtConnection *conn, double corruption_rate) {
    memset(conn, 0, sizeof(*conn));
    conn->running = true;
    conn->corruption_rate = corruption_rate;
    rt_queue_init(&conn->outbound);
    rt_queue_init(&conn->inbound);
    pthread_mutex_init(&conn->ack_mu, NULL);
    pthread_cond_init(&conn->ack_ready, NULL);

    if (pthread_create(&conn->sender_thread, NULL, sender_main, conn) != 0) {
        return -1;
    }
    if (pthread_create(&conn->receiver_thread, NULL, receiver_main, conn) != 0) {
        conn->running = false;
        return -1;
    }
    return 0;
}

void rt_connection_stop(RtConnection *conn) {
    RtPacket stop = {.type = RT_STOP};
    rt_queue_push(&conn->outbound, &stop);
    rt_queue_close(&conn->outbound);

    pthread_mutex_lock(&conn->ack_mu);
    conn->running = false;
    pthread_cond_broadcast(&conn->ack_ready);
    pthread_mutex_unlock(&conn->ack_mu);

    pthread_join(conn->sender_thread, NULL);
    pthread_join(conn->receiver_thread, NULL);
    rt_queue_close(&conn->inbound);
}

void rt_send_message(RtConnection *conn, const char *message) {
    RtPacket *packets = NULL;
    size_t count = rt_packetize(message, &packets);
    for (size_t i = 0; i < count; i++) {
        rt_queue_push(&conn->outbound, &packets[i]);
    }
    free(packets);
}

void rt_run_demo(void) {
    srand(7);
    RtConnection conn;
    if (rt_connection_start(&conn, 0.25) != 0) {
        fprintf(stderr, "failed to start transport demo\n");
        return;
    }

    rt_send_message(&conn, "systems programming combines memory, processes, threads, and reliable messages");
    sleep(1);
    rt_connection_stop(&conn);
    printf("transport demo complete\n");
}
