#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __packed __attribute__((packed))
typedef int spinlock_t;
typedef int wait_queue_head_t;
typedef int atomic_t;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
struct mutex { int held; };
/* PRODUCTION_HEADERS */
/* PRODUCTION_VALIDATOR */
#define TP_PROTO(...) __VA_ARGS__
#define TP_ARGS(...) __VA_ARGS__
#define TP_STRUCT__entry(...) __VA_ARGS__
#define __field(type, name) type name;
#define TP_fast_assign(...) __VA_ARGS__
#define TP_printk(...)
#define TRACE_EVENT(name, proto, args, structure, assign, print) \
    struct captured_event { structure }; \
    static struct captured_event captured; \
    static void trace_##name(proto) { struct captured_event *__entry = &captured; assign; }
/* PRODUCTION_TRACEPOINT */
int main(void) {
    u32 storage[32];
    struct apr_client_data data = {0};
    assert(sizeof(struct asm_data_event_read_done_v2) == 44);
    data.opcode = ASM_DATA_EVENT_READ_DONE_V2; data.payload = storage;
    memset(storage, 0xa5, sizeof(storage));
    for (unsigned length = 0; length < 44; length++) { data.payload_size = length; assert(!q6asm_payload_valid(&data)); }
    data.payload_size = 44; assert(q6asm_payload_valid(&data));
    data.payload = NULL; assert(!q6asm_payload_valid(&data));
    data.opcode = RESET_EVENTS; data.payload_size = 0; assert(q6asm_payload_valid(&data));
    data.payload = storage; data.opcode = APR_BASIC_RSP_RESULT; data.payload_size = 4; assert(!q6asm_payload_valid(&data));
    data.payload_size = 8; assert(q6asm_payload_valid(&data));
    data.opcode = ASM_DATA_EVENT_WRITE_DONE_V2; data.payload_size = 12; assert(!q6asm_payload_valid(&data));
    data.payload_size = 16; assert(q6asm_payload_valid(&data));
    data.opcode = ASM_SESSION_CMDRSP_GET_SESSIONTIME_V3; data.payload_size = 16; assert(!q6asm_payload_valid(&data));
    data.payload_size = 20; assert(q6asm_payload_valid(&data));
    for (unsigned index = 0; index < 11; index++) storage[index] = 0x12300000 + index;
    trace_q6asm_read_done((void *)(uintptr_t)0x4000, (void *)(uintptr_t)0x8000, 0x17, 44, storage);
    assert(captured.audio == 0x4000 && captured.audio_client == 0x8000 && captured.token == 0x17 && captured.payload_bytes == 44);
    const u32 actual[] = { captured.status, captured.physical_lsw, captured.physical_msw, captured.map_handle, captured.total_bytes, captured.offset, captured.timestamp_lsw, captured.timestamp_msw, captured.flags, captured.frames, captured.sequence_id };
    assert(memcmp(actual, storage, sizeof(actual)) == 0);
    puts("PASS: real Q6ASM payload validator rejects readable stale tails; tracepoint copies all eleven original ABI words");
    return 0;
}
