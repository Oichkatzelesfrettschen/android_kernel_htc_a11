#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __packed __attribute__((packed))
typedef int spinlock_t;
typedef int wait_queue_head_t;
typedef int atomic_t;
struct mutex { int held; };
struct smd_channel { unsigned char bytes[20000]; int total; int position; int available; int short_read; int queued_total; };
#define SMD_EVENT_DATA 1
#define SMD_EVENT_OPEN 2
#define SMD_EVENT_CLOSE 3
#define spin_lock_irqsave(lock, flags) do { (void)(lock); (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
#define mutex_lock(lock) ((void)(lock))
#define mutex_unlock(lock) ((void)(lock))
#define wake_up(wait) ((void)(wait))
static void diagnostic(const char *format, ...) { (void)format; }
#define pr_debug diagnostic
#define pr_err diagnostic
/* PRODUCTION_HEADERS */
static struct apr_client client[APR_DEST_MAX][APR_CLIENT_MAX];
static struct apr_client_data observed;
static unsigned callbacks;
static int received_length;
static int callback(struct apr_client_data *data, void *priv) { observed = *data; callbacks++; return 0; }
static void receive(void *data, int length, void *priv) { received_length = length; callbacks++; assert(((unsigned char *)data)[length - 1] == 0x5a); }
static int smd_read_avail(struct smd_channel *channel) { int available = channel->available - channel->position; int remainder = channel->total - channel->position; return available < remainder ? available : remainder; }
static int smd_cur_packet_size(struct smd_channel *channel) { return channel->total - channel->position; }
static int smd_write_avail(struct smd_channel *channel) { return 0; }
static int smd_read_from_cb(struct smd_channel *channel, void *buffer, int length) {
    int count = length;
    if (channel->short_read && count > channel->short_read) count = channel->short_read;
    assert(count >= 0 && count <= channel->available - channel->position);
    if (buffer) memcpy(buffer, channel->bytes + channel->position, count);
    channel->position += count;
    if (channel->position == channel->total && channel->queued_total) { channel->total = channel->queued_total; channel->queued_total = 0; }
    return count;
}
/* PRODUCTION_FUNCTIONS */
static void test_apr(void) {
    unsigned char bytes[128] = {0};
    struct apr_hdr *header = (struct apr_hdr *)bytes;
    struct apr_svc *service = &client[APR_DEST_QDSP6][APR_CLIENT_AUDIO].svc[0];
    service->id = APR_SVC_ASM; service->fn = callback;
    header->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT, 5, APR_PKT_VER);
    header->src_domain = APR_DOMAIN_ADSP; header->dest_domain = APR_DOMAIN_APPS;
    header->src_svc = APR_SVC_ASM; header->dest_svc = APR_SVC_ASM;
    header->pkt_size = 64;
    callbacks = 0; apr_cb_func(bytes, 28, NULL); assert(callbacks == 0);
    header->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT, 15, APR_PKT_VER);
    header->pkt_size = 20; apr_cb_func(bytes, 64, NULL); assert(callbacks == 0);
    header->hdr_field = APR_HDR_FIELD(APR_MSG_TYPE_EVENT, 5, APR_PKT_VER);
    header->pkt_size = 20; apr_cb_func(bytes, 20, NULL);
    assert(callbacks == 1 && observed.payload_size == 0 && observed.payload == NULL && observed.hdr_len == 20);
    header->pkt_size = 64; callbacks = 0; apr_cb_func(bytes, 64, NULL);
    assert(callbacks == 1 && observed.payload_size == 44 && observed.payload == bytes + 20);
    header->src_port = 0xffff; callbacks = 0; service->port_cnt = 1;
    apr_cb_func(bytes, 64, NULL); assert(callbacks == 0);
    service->port_cnt = 0; callbacks = 0;
    apr_cb_func(bytes, 64, NULL); assert(callbacks == 1); callbacks = 0;
    apr_cb_func(NULL, 64, NULL); apr_cb_func(bytes, -1, NULL); apr_cb_func(bytes, 19, NULL);
    assert(callbacks == 0);
    memset(&observed, 0xa5, sizeof(observed));
    dispatch_event(7, APR_DEST_QDSP6);
    assert(callbacks == 1 && observed.opcode == RESET_EVENTS && observed.payload == NULL && observed.payload_size == 0 && observed.token == 0);
}
static void test_tal(void) {
    struct smd_channel channel = {0};
    struct apr_svc_ch_dev transport = {0};
    transport.ch = &channel; transport.func = receive;
    channel.total = 100; channel.available = 40; memset(channel.bytes, 0x5a, sizeof(channel.bytes));
    callbacks = 0; apr_tal_notify(&transport, SMD_EVENT_DATA); assert(callbacks == 0 && channel.position == 40);
    channel.available = 100; apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 1 && received_length == 100);
    channel.position = 0; channel.total = APR_MAX_BUF; channel.available = APR_MAX_BUF; callbacks = 0;
    apr_tal_notify(&transport, SMD_EVENT_DATA); assert(callbacks == 1 && received_length == APR_MAX_BUF);
    channel.position = 0; channel.total = APR_MAX_BUF + 1; channel.available = 4000; callbacks = 0;
    apr_tal_notify(&transport, SMD_EVENT_DATA); assert(callbacks == 0 && channel.position == 4000);
    channel.available = channel.total; apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 0 && channel.position == channel.total);
    channel.position = 0; channel.total = 100; channel.available = 100;
    apr_tal_notify(&transport, SMD_EVENT_DATA); assert(callbacks == 1 && received_length == 100);
    channel.position = 0; channel.total = 100; channel.available = 40; callbacks = 0;
    apr_tal_notify(&transport, SMD_EVENT_DATA); apr_tal_notify(&transport, SMD_EVENT_CLOSE);
    channel.position = 0; channel.total = 60; channel.available = 60;
    apr_tal_notify(&transport, SMD_EVENT_OPEN); apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 1 && received_length == 60);
    channel.position = 0; channel.total = 100; channel.available = 100; channel.short_read = 20; callbacks = 0;
    for (unsigned attempt = 0; attempt < 5; attempt++) apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 1 && received_length == 100);
    channel.position = 0; channel.total = 60; channel.available = 160; channel.queued_total = 160; channel.short_read = 0; callbacks = 0;
    apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 2 && received_length == 100);
    channel.position = 0; channel.total = APR_MAX_BUF + 1; channel.available = APR_MAX_BUF + 1; channel.short_read = 333; callbacks = 0;
    for (unsigned attempt = 0; attempt < 30; attempt++) apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 0 && channel.position == channel.total);
    channel.position = 0; channel.total = APR_MAX_BUF + 1; channel.available = 4000; channel.short_read = 0;
    apr_tal_notify(&transport, SMD_EVENT_DATA); apr_tal_notify(&transport, SMD_EVENT_CLOSE);
    channel.position = 0; channel.total = 60; channel.available = 60;
    apr_tal_notify(&transport, SMD_EVENT_OPEN); apr_tal_notify(&transport, SMD_EVENT_DATA);
    assert(callbacks == 1 && received_length == 60);
}
int main(void) { test_apr(); test_tal(); puts("PASS: real APR/TAL malformed, header-only, reset, fragmented, capacity, discard and short-read paths"); return 0; }
