/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;
#define __packed __attribute__((packed))
#define TRUE 1
#define HZ 100
#include "q6asm-types.h"

typedef struct { int value; } atomic_t;
struct apr_svc { uint8_t id; };
struct apr_client_data { uint32_t opcode, token; uint32_t *payload; };
struct audio_client {
	void *apr;
	int session, stream_id, io_mode, cmd_lock, cmd_wait;
	atomic_t cmd_state, cmd_status;
	void (*cb)(uint32_t, uint32_t, uint32_t *, void *);
	void *priv;
};

static int atomic_read(const atomic_t *value) { return value->value; }
static void atomic_set(atomic_t *value, int result) { value->value = result; }
static void fixture_log(const char *format, ...) { (void)format; }
#define pr_debug fixture_log
#define pr_info fixture_log
#define pr_err fixture_log
#define mutex_lock(lock) ((void)(lock))
#define mutex_unlock(lock) ((void)(lock))
#define smp_rmb() ((void)0)
#define smp_wmb() ((void)0)
#define wake_up(queue) ((void)(queue))

static unsigned int fixture_waits;
static uint32_t fixture_token;
static int fixture_wait(int queue, int condition, int timeout)
{
	(void)queue;
	assert(timeout == 5 * HZ);
	fixture_waits++;
	return condition;
}
#define wait_event_timeout(queue, condition, timeout) \
	fixture_wait(queue, condition, timeout)
static int apr_send_pkt(void *apr, uint32_t *packet);
#include "q6asm-functions.h"

struct scenario {
	const char *name;
	uint16_t format, aot, channels;
	uint32_t rate, response;
	int send_result, timeout, expected_result;
};

static const struct scenario *active_scenario;
static struct audio_client *active_client;
static unsigned char sent_packet[36];
static unsigned int fixture_sends, fixture_callbacks;
static int status_at_send, state_at_send;

static void record_callback(uint32_t opcode, uint32_t token,
			    uint32_t *payload, void *private_data)
{
	assert(opcode == APR_BASIC_RSP_RESULT);
	assert(token == fixture_token);
	assert(payload[0] == ASM_DATA_CMD_MEDIA_FMT_UPDATE_V2);
	assert(payload[1] == active_scenario->response);
	assert(private_data == active_client);
	fixture_callbacks++;
}

static int apr_send_pkt(void *apr, uint32_t *packet)
{
	struct apr_hdr header;

	assert(apr == active_client->apr);
	memcpy(&header, packet, sizeof(header));
	assert(header.pkt_size == sizeof(sent_packet));
	memcpy(sent_packet, packet, sizeof(sent_packet));
	fixture_token = header.token;
	fixture_sends++;
	status_at_send = atomic_read(&active_client->cmd_status);
	state_at_send = atomic_read(&active_client->cmd_state);
	if (active_scenario->send_result < 0)
		return active_scenario->send_result;
	if (!active_scenario->timeout)
		fixture_complete(active_client, active_scenario->response);
	return 0;
}

static void store_le16(unsigned char *bytes, uint16_t value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
}

static void store_le32(unsigned char *bytes, uint32_t value)
{
	store_le16(bytes, value);
	store_le16(bytes + 2, value >> 16);
}

static unsigned int failures, cases;
static void check_result(const char *name, int wrapper, int compressed,
			 const char *field, int expected, int actual)
{
	if (actual == expected)
		return;
	fprintf(stderr, "%s wrapper=%d compressed=%d %s: expected %d, got %d\n",
		name, wrapper, compressed, field, expected, actual);
	failures++;
}

static void run_case(const struct scenario *scenario, int wrapper, int compressed)
{
	struct apr_svc service = { .id = 7 };
	struct audio_client client = {
		.apr = &service, .session = 3, .stream_id = 1,
		.io_mode = compressed ? COMPRESSED_STREAM_IO : 0,
		.cmd_status = { ADSP_EBADPARAM }, .cb = record_callback,
	};
	struct asm_aac_cfg config = {
		.format = scenario->format, .aot = scenario->aot,
		.ch_cfg = scenario->channels, .sample_rate = scenario->rate,
	};
	/* APR sequential command, 20-byte header and 12-byte AAC block. */
	unsigned char expected_packet[36] = {
		0x50, 0x02, 0x24, 0x00, 0x07, 0x05, 0x01, 0x03,
		0x07, 0x04, 0x01, 0x03, 0x03, 0x00, 0x00, 0x00,
		0x98, 0x0d, 0x01, 0x00, 0x0c, 0x00, 0x00, 0x00,
	};
	int stream = wrapper == 2 ? 2 : 1;
	int result;

	active_scenario = scenario;
	active_client = &client;
	client.priv = &client;
	fixture_waits = fixture_sends = fixture_callbacks = 0;
	memset(sent_packet, 0xff, sizeof(sent_packet));
	expected_packet[6] = expected_packet[10] = stream;
	if (compressed) {
		expected_packet[12] = stream;
		expected_packet[13] = 3;
	}
	store_le16(expected_packet + 24, scenario->format);
	store_le16(expected_packet + 26, scenario->aot);
	store_le16(expected_packet + 28, scenario->channels);
	store_le32(expected_packet + 32, scenario->rate);

	if (wrapper == 0)
		result = q6asm_media_format_block_aac(&client, &config);
	else if (wrapper == 1)
		result = q6asm_media_format_block_multi_aac(&client, &config);
	else
		result = q6asm_stream_media_format_block_aac(&client, &config, 2);
	cases++;
#define CHECK(field, expected, actual) \
	check_result(scenario->name, wrapper, compressed, field, expected, actual)
	CHECK("result", scenario->expected_result, result);
	CHECK("APR sends", 1, fixture_sends);
	CHECK("packet bytes", 0, memcmp(expected_packet, sent_packet, sizeof(sent_packet)));
	CHECK("armed command", 1, state_at_send);
	CHECK("cleared prior status", 0, status_at_send);
	CHECK("wait calls", scenario->send_result < 0 ? 0 : 1, fixture_waits);
	CHECK("callback calls", scenario->send_result < 0 || scenario->timeout ? 0 : 1,
	      fixture_callbacks);
#undef CHECK
}

int main(void)
{
	static const struct scenario scenarios[] = {
		{ "mono ADTS EOK", 0, 2, 1, 44100, 0, 0, 0, 0 },
		{ "stereo RAW EOK", 3, 2, 2, 48000, 0, 0, 0, 0 },
		{ "5.1 RAW EOK", 3, 2, 6, 48000, 0, 0, 0, 0 },
		{ "HE-AAC LOAS EOK", 1, 5, 2, 24000, 0, 0, 0, 0 },
		{ "HE-AACv2 ADIF EOK", 2, 29, 2, 24000, 0, 0, 0, 0 },
		{ "6.1 firmware EOK", 3, 2, 7, 48000, 0, 0, 0, 0 },
		{ "6.1 EBADPARAM", 3, 2, 7, 48000, 2, 0, 0, -EINVAL },
		{ "EALREADY", 3, 2, 2, 48000, 9, 0, 0, 0 },
		{ "APR send error", 3, 2, 2, 48000, 0, -EPIPE, 0, -EPIPE },
		{ "response timeout", 3, 2, 2, 48000, 0, 0, 1, -ETIMEDOUT },
	};
	for (size_t index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]); index++)
		for (int wrapper = 0; wrapper < 3; wrapper++)
			for (int compressed = 0; compressed < 2; compressed++)
				run_case(&scenarios[index], wrapper, compressed);
	printf("AAC format: %u cases, %u failed assertions\n", cases, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
