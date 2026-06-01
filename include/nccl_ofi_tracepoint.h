/*
 * Copyright (c) 2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 */


#ifndef NCCL_OFI_TRACEPOINT_H_
#define NCCL_OFI_TRACEPOINT_H_

#include "config.h"
#include "tracing_impl/nvtx.h"
#include "tracing_impl/lttng.h"
#include "tracing_impl/esp_tracing_impl.h"

#if HAVE_LIBESP == 1

#include "nccl_ofi.h"

extern void esp_handle_send_end(void* req_ptr);
extern void esp_handle_write(void* req_ptr, void* recv_data_ptr, uint8_t recv_idx, uint64_t total_segms);
extern void esp_handle_req_complete(void* req_ptr, int ncompls, int total_ncompls);
extern void esp_handle_request_assembly(void* req_ptr);
extern void esp_track_rail_bw(int dev_id, nccl_net_ofi_device_t *device);

// send handling
#define ESP_HANDLE_SEND(req, size) \
	ESP_BEGIN_BATCH_REPORT() \
	{ \
		ESP_REPORT_COUNTER_INC_TIME(ofi_inflight_write_bytes, size, ESP_CURR_TIME); \
		ESP_REPORT_COUNTER_INC_TIME(ofi_inflight_write_count, 1, ESP_CURR_TIME); \
		req->req_start_time = ESP_CURR_TICK_COUNT; \
	} \
	ESP_END_BATCH_REPORT()

// send-end handling
#define ESP_HANDLE_SEND_END(req) esp_handle_send_end(req)

// fi_write and fi_writedata handling
#if ENABLE_ESP_BW == 1
#define ESP_BEGIN_FI_WRITE(xfer_info) \
		ESP_BEGIN_BATCH_REPORT(); \
		ESP_REPORT_BW_METER_TIME(fi_write, ESP_CURR_TIME, xfer_info->msg_size); \
		ESP_SYNC_HISTORGRAM_START_TIME(fi_write, \
			ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true, ESP_CURR_TICK_COUNT); \
		ESP_END_BATCH_REPORT();

#define ESP_END_FI_WRITE() \
	ESP_SYNC_HISTORGRAM_END(fi_write, ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true)

#define ESP_BEGIN_FI_WRITE_DATA(xfer_info) \
		ESP_BEGIN_BATCH_REPORT(); \
		ESP_REPORT_BW_METER_TIME(fi_writedata, ESP_CURR_TIME, xfer_info->msg_size); \
		ESP_SYNC_HISTORGRAM_START_TIME(fi_writedata, \
			ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true, ESP_CURR_TICK_COUNT); \
		ESP_END_BATCH_REPORT();

#define ESP_END_FI_WRITE_DATA() \
	ESP_SYNC_HISTORGRAM_END(fi_writedata, ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true)
#else
#define ESP_BEGIN_FI_WRITE(xfer_info) \
		ESP_SCOPED_LATENCY_HISTOGRAM(fi_write, \
			ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true);


#define ESP_END_FI_WRITE()

#define ESP_BEGIN_FI_WRITEDATA(xfer_info) \
		ESP_SCOPED_LATENCY_HISTOGRAM(fi_writedata, \
			ESP_LINEAR_BIN_GENERATOR(100, 10, 0), true);

#define ESP_END_FI_WRITE_DATA()
#endif

// gin helper macros
#define ESP_SET_REQ_START_TIME(req) req->req_start_time = ESP_GET_WALL_CLOCK()
#define ESP_GET_REQ_START_TIME(req) req->req_start_time
#define ESP_RESET_REQ_START_TIME(req) req->req_start_time = 0

#define ESP_GIN_SET_REQ_START_TIME(req) req->set_start_time()
#define ESP_GIN_GET_REQ_START_TIME(req) req->get_start_time()

#else

#define ESP_HANDLE_SEND(req, size)
#define ESP_HANDLE_SEND_END(req)
#define ESP_BEGIN_FI_WRITE(xfer_info)
#define ESP_END_FI_WRITE()
#define ESP_BEGIN_FI_WRITE_DATA(xfer_info)
#define ESP_END_FI_WRITE_DATA()

#define ESP_SET_REQ_START_TIME(req) req->req_start_time = ESP_GET_WALL_CLOCK()
#define ESP_GET_REQ_START_TIME(req) req->req_start_time
#define ESP_RESET_REQ_START_TIME(req) req->req_start_time = 0

#define ESP_GIN_SET_REQ_START_TIME(req)
#define ESP_GIN_GET_REQ_START_TIME(req) 0
#endif

/***** SENDRECV PROTOCOL *****/
#define NCCL_OFI_TRACE_SEND_SENDRECV(dev, size, comm, msg_seq_num, request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send, dev, size, comm, 0, msg_seq_num, request, nccl_req, 0, 0); \
} while (0)

#define NCCL_OFI_TRACE_RECV_SENDRECV(dev, comm, size, request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Recv, dev, comm, 0, size, request, nccl_req, 0); \
} while(0)

#define NCCL_OFI_TRACE_FLUSH_SENDRECV(request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Flush, request, nccl_req); \
} while(0)

#define NCCL_OFI_TRACE_COMPLETIONS_SENDRECV(dev,req_direction,request,ctx) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, ProcessCompletionsSendRecv, dev,req_direction,request,ctx); \
} while(0)

/***** RDMA PROTOCL *****/

#define NCCL_OFI_TRACE_SEND(dev, size, comm, msg_seq_num, request, nccl_req, tag, recv_idx) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send, dev, size, comm,\
			     comm->get_data_rail(0)->remote_addr, \
			     msg_seq_num, request, nccl_req, tag, recv_idx); \
	NCCL_OFI_TRACE_SEND_NVTX(dev, size, comm, msg_seq_num, request, nccl_req); \
	ESP_HANDLE_SEND(request, size); \
} while(0)

#define NCCL_OFI_TRACE_SEND_END(dev, comm, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, SendEnd, dev, comm, request); \
	NCCL_OFI_TRACE_SEND_END_NVTX(request); \
	ESP_HANDLE_SEND_END(request); \
} while(0)

#define NCCL_OFI_TRACE_EAGER_SEND_START(dev, rail_id, size, comm, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send_eager_start, dev, rail_id, size, comm, msg_seq_num, request); \
	NCCL_OFI_TRACE_EAGER_SEND_START_NVTX(dev, rail_id, size, comm, msg_seq_num, request); \
} while(0)

#define NCCL_OFI_TRACE_EAGER_SEND_COMPLETE(dev, rail_id, comm, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send_eager_complete, dev, rail_id, comm, msg_seq_num, request); \
	NCCL_OFI_TRACE_EAGER_SEND_COMPLETE_NVTX(dev, rail_id, comm, msg_seq_num, request); \
} while (0)

#define NCCL_OFI_TRACE_WRITE_CTRL_START(dev, rail_id, comm, req, msg_seq_num) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Write_ctrl_start, dev, rail_id, comm, req, msg_seq_num); \
	NCCL_OFI_TRACE_WRITE_CTRL_START_NVTX(dev, rail_id, comm, req, msg_seq_num); \
} while (0);

#define NCCL_OFI_TRACE_WRITE_CTRL_END(dev, rail_id, comm, req, msg_seq_num) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Write_ctrl_end, dev, rail_id, comm, req, msg_seq_num); \
	NCCL_OFI_TRACE_WRITE_CTRL_END_NVTX(dev, rail_id, comm, req, msg_seq_num); \
} while (0);

#define NCCL_OFI_TRACE_SEND_WRITE_SEG_START(dev, rail_id, size, comm, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send_write_segment_start, dev, rail_id, size, comm, msg_seq_num, request); \
	NCCL_OFI_TRACE_SEND_WRITE_SEG_START_NVTX(dev, rail_id, size, comm, msg_seq_num, request); \
} while(0)

#define NCCL_OFI_TRACE_SEND_WRITE_SEG_COMPLETE(dev, rail_id, comm, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Send_write_segment_complete, dev, rail_id, comm, msg_seq_num, request); \
	NCCL_OFI_TRACE_SEND_WRITE_SEG_COMPLETE_NVTX(dev, rail_id, comm, msg_seq_num, request); \
} while(0)

#define NCCL_OFI_TRACE_RECV(dev, comm, size, request, nccl_req, num_recvs) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Recv, dev, comm, \
			     comm->get_data_rail(0)->remote_addr, size, request, nccl_req, num_recvs); \
	NCCL_OFI_TRACE_RECV_NVTX(dev, comm, size, request, nccl_req); \
	ESP_SET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_RECV_END(dev, comm, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, RecvEnd, dev, comm, request); \
	NCCL_OFI_TRACE_RECV_END_NVTX(request); \
	ESP_ASYNC_HISTOGRAM_SAMPLE(ofi_rx_latency, \
		ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, true, "ns", 1, \
		ESP_GET_WALL_CLOCK() - ESP_GET_REQ_START_TIME(request)); \
		ESP_RESET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_RECV_SEGMENT_COMPLETE(dev, rail_id, comm, size, request, msg_seq_num, recv_idx) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Recv_segment_complete, dev, rail_id, comm, size, request, msg_seq_num, recv_idx); \
	NCCL_OFI_TRACE_RECV_SEGMENT_COMPLETE_NVTX(dev, rail_id, size, request, msg_seq_num); \
} while(0)

#define NCCL_OFI_TRACE_EAGER_RECV(dev, rail_id, comm, msg_seq_num, tag) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Eager_recv, dev, rail_id, comm, msg_seq_num, tag); \
	NCCL_OFI_TRACE_EAGER_RECV_NVTX(dev, rail_id, comm, msg_seq_num); \
} while(0)

#define NCCL_OFI_TRACE_COMPLETIONS(dev,req_type,request,ctx) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, ProcessCompletionsRdma, dev,req_type,request,ctx); \
} while(0)

#define NCCL_OFI_TRACE_FLUSH(request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Flush, request, nccl_req); \
	NCCL_OFI_TRACE_FLUSH_NVTX(request, nccl_req); \
} while(0)

#define NCCL_OFI_TRACE_READ(request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Read, request, nccl_req); \
	NCCL_OFI_TRACE_READ_NVTX(request, nccl_req); \
} while(0)

#define NCCL_OFI_TRACE_WRITE(request, nccl_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Write, request, nccl_req); \
	NCCL_OFI_TRACE_WRITE_NVTX(request, nccl_req); \
} while(0)

#define NCCL_OFI_TRACE_PENDING_INSERT(request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Pending_queue_insert, request); \
	NCCL_OFI_TRACE_PENDING_INSERT_NVTX(request); \
} while(0)

#define NCCL_OFI_TRACE_PENDING_REMOVE(request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, Pending_queue_remove, request); \
	NCCL_OFI_TRACE_PENDING_REMOVE_NVTX(request); \
} while(0)

/***** GIN PROTOCOL *****/

#define NCCL_OFI_TRACE_GIN_IPUT_SIGNAL_BEGIN(dev, size, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_iput_signal_begin, dev, size, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_IPUT_SIGNAL_BEGIN_NVTX(comm, rank, msg_seq_num, size, request); \
	ESP_GIN_SET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_GIN_IPUT_SIGNAL_END(dev, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_iput_signal_end, dev, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_IPUT_SIGNAL_END_NVTX(request); \
	ESP_ASYNC_HISTOGRAM_SAMPLE(gin_iput_signal, \
		ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, true, "ns", 1, \
		ESP_CURR_TICK_COUNT - ESP_GIN_GET_REQ_START_TIME(request)); \
} while(0)

#define NCCL_OFI_TRACE_GIN_WRITE_BEGIN(dev, rail_id, size, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_write_begin, dev, rail_id, size, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_WRITE_BEGIN_NVTX(comm, rail_id, rank, msg_seq_num, size, request); \
	ESP_GIN_SET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_GIN_WRITE_END(dev, rail_id, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_write_end, dev, rail_id, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_WRITE_END_NVTX(comm, msg_seq_num, request); \
	ESP_ASYNC_HISTOGRAM_SAMPLE(gin_write, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, true, "ns", 1, \
		ESP_GET_WALL_CLOCK() - ESP_GIN_GET_REQ_START_TIME(request)); \
} while(0)

#define NCCL_OFI_TRACE_GIN_METADATA_SEND_BEGIN(dev, rail_id, size, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_metadata_send_begin, dev, rail_id, size, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_METADATA_SEND_BEGIN_NVTX(comm, rail_id, rank, msg_seq_num, size, request); \
	ESP_GIN_SET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_GIN_METADATA_SEND_END(dev, rail_id, size, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_metadata_send_end, dev, rail_id, size, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_METADATA_SEND_END_NVTX(comm, msg_seq_num, request); \
	ESP_ASYNC_HISTOGRAM_SAMPLE(gin_metadata_send, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, true, "ns", 1, \
		ESP_GET_WALL_CLOCK() - ESP_GIN_GET_REQ_START_TIME(request)); \
} while(0)

#define NCCL_OFI_TRACE_GIN_RECV_WRITE(dev, rail_id, size, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_recv_write, dev, rail_id, size, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_RECV_WRITE_NVTX(comm, rail_id, rank, msg_seq_num, size, request); \
	ESP_SYNC_HISTOGRAM_SAMPLE(gin_rec_write_size, ESP_LOG2_BIN_GENERATOR(), true, size); \
} while(0)


#define NCCL_OFI_TRACE_GIN_SIGNAL_DELIVERY_BEGIN(dev, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_signal_delivery_begin, dev, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_SIGNAL_DELIVERY_BEGIN_NVTX(comm, rank, msg_seq_num, request); \
	ESP_GIN_SET_REQ_START_TIME(request); \
} while(0)

#define NCCL_OFI_TRACE_GIN_SIGNAL_DELIVERY_END(dev, comm, rank, msg_seq_num, request) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_signal_delivery_end, dev, comm, rank, msg_seq_num, request); \
	NCCL_OFI_TRACE_GIN_SIGNAL_DELIVERY_END_NVTX(comm, msg_seq_num, request); \
	ESP_ASYNC_HISTOGRAM_SAMPLE(gin_signal_delivery, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, true, "ns", 1, \
		ESP_GET_WALL_CLOCK() - ESP_GIN_GET_REQ_START_TIME(request)); \
} while(0)

#define NCCL_OFI_TRACE_GIN_ACK_RECV(dev, rail_id, comm, rank, msg_seq_num) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_ack_recv, dev, rail_id, comm, rank, msg_seq_num); \
	NCCL_OFI_TRACE_GIN_ACK_RECV_NVTX(comm, rail_id, rank, msg_seq_num); \
} while(0)

#define NCCL_OFI_TRACE_GIN_ACK_SEND(dev, rail_id, comm, rank, msg_seq_num) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_ack_send, dev, rail_id, comm, rank, msg_seq_num); \
	NCCL_OFI_TRACE_GIN_ACK_SEND_NVTX(comm, rail_id, rank, msg_seq_num); \
} while(0)

#define NCCL_OFI_TRACE_GIN_TEST_FUNC_BEGIN(dev, comm, peer_rank, msg_seq_num, request, has_write_reqs, has_send_req) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_test_func_begin, dev, comm, peer_rank, msg_seq_num, request, has_write_reqs, has_send_req); \
	ESP_SYNC_HISTORGRAM_START(gin_test_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_TEST_FUNC_END(dev, comm, peer_rank, msg_seq_num, request, done, freed) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_test_func_end, dev, comm, peer_rank, msg_seq_num, request, done, freed); \
	ESP_SYNC_HISTORGRAM_END_TIME(gin_test_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true, ESP_CURR_TICK_COUNT); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_SIGNAL_WRITE_COMPLETION_FUNC_START(dev, comm, rail_id, peer_rank, msg_seq_num, total_segms, len, is_ack_requested) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_signal_write_completion_func_start, dev, comm, rail_id, peer_rank, msg_seq_num, total_segms, len, is_ack_requested); \
	ESP_SYNC_HISTORGRAM_START(gin_handle_signal_write_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_SIGNAL_WRITE_COMPLETION_FUNC_END(dev, comm, rail_id, peer_rank, msg_seq_num, ret) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_signal_write_completion_func_end, dev, comm, rail_id, peer_rank, msg_seq_num, ret); \
	ESP_SYNC_HISTORGRAM_END(gin_handle_signal_write_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_SIGNAL_METADATA_COMPLETION_FUNC_START(dev, comm, rail_id, peer_rank, msg_seq_num, num_segments) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_signal_metadata_completion_func_start, dev, comm, rail_id, peer_rank, msg_seq_num, num_segments); \
    ESP_SYNC_HISTORGRAM_START(gin_handle_signal_metadata_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_SIGNAL_METADATA_COMPLETION_FUNC_END(dev, comm, rail_id, peer_rank, msg_seq_num, ret) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_signal_metadata_completion_func_end, dev, comm, rail_id, peer_rank, msg_seq_num, ret); \
	ESP_SYNC_HISTORGRAM_END(gin_handle_signal_metadata_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_ACK_COMPLETION_FUNC_START(dev, comm, rail_id, peer_rank, consumed) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_ack_completion_func_start, dev, comm, rail_id, peer_rank, consumed); \
    ESP_SYNC_HISTORGRAM_START(gin_handle_ack_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#define NCCL_OFI_TRACE_GIN_HANDLE_ACK_COMPLETION_FUNC_END(dev, comm, rail_id, peer_rank, consumed, ret) do { \
	lttng_ust_tracepoint(nccl_ofi_plugin, gin_handle_ack_completion_func_end, dev, comm, rail_id, peer_rank, consumed, ret); \
	ESP_SYNC_HISTORGRAM_END(gin_handle_ack_completion_func, ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000), true); \
} while(0)

#endif /* NCCL_OFI_TRACEPOINT_H_ */
