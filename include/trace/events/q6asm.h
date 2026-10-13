#undef TRACE_SYSTEM
#define TRACE_SYSTEM q6asm

#if !defined(_TRACE_Q6ASM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_Q6ASM_H

#include <linux/tracepoint.h>

/* The producer validates the complete READ_DONE ABI before emitting fields. */
TRACE_EVENT(q6asm_read_done,
	TP_PROTO(void *audio, void *audio_client, u32 token,
		 u32 payload_bytes, const u32 *payload),
	TP_ARGS(audio, audio_client, token, payload_bytes, payload),
	TP_STRUCT__entry(
		__field(u32, audio)
		__field(u32, audio_client)
		__field(u32, token)
		__field(u32, payload_bytes)
		__field(u32, status)
		__field(u32, physical_lsw)
		__field(u32, physical_msw)
		__field(u32, map_handle)
		__field(u32, total_bytes)
		__field(u32, offset)
		__field(u32, timestamp_lsw)
		__field(u32, timestamp_msw)
		__field(u32, flags)
		__field(u32, frames)
		__field(u32, sequence_id)
	),
	TP_fast_assign(
		__entry->audio = (unsigned long)audio;
		__entry->audio_client = (unsigned long)audio_client;
		__entry->token = token;
		__entry->payload_bytes = payload_bytes;
		__entry->status = payload[0];
		__entry->physical_lsw = payload[1];
		__entry->physical_msw = payload[2];
		__entry->map_handle = payload[3];
		__entry->total_bytes = payload[4];
		__entry->offset = payload[5];
		__entry->timestamp_lsw = payload[6];
		__entry->timestamp_msw = payload[7];
		__entry->flags = payload[8];
		__entry->frames = payload[9];
		__entry->sequence_id = payload[10];
	),
	TP_printk("audio=%08x audio_client=%08x token=%08x payload_bytes=%08x status=%08x physical_lsw=%08x physical_msw=%08x map_handle=%08x total_bytes=%08x offset=%08x timestamp_lsw=%08x timestamp_msw=%08x flags=%08x frames=%08x sequence_id=%08x",
		__entry->audio,
		__entry->audio_client,
		__entry->token,
		__entry->payload_bytes,
		__entry->status,
		__entry->physical_lsw,
		__entry->physical_msw,
		__entry->map_handle,
		__entry->total_bytes,
		__entry->offset,
		__entry->timestamp_lsw,
		__entry->timestamp_msw,
		__entry->flags,
		__entry->frames,
		__entry->sequence_id)
);

#endif

#include <trace/define_trace.h>
