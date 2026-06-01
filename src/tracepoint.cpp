/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <config.h>
#if HAVE_LIBLTTNG_UST == 1

#define TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE

/*
 * tracepoint.c creates the lttng probes, from those created in tracepoint.h.  The probes are created
 * only if TRACEPOINT_CREATE_PROBES is set before the definitions of those probes, so tracepoint.h must
 * be included once after that definition.
 *
 */

#include <tracing_impl/lttng.h>

#endif // HAVE_LIBLTTNG_UST == 1

#if HAVE_LIBESP == 1
#include "nccl_ofi_tracepoint.h"
#include "nccl_ofi_rdma.h"

static std::mutex track_rail_lock;
static bool rate_var_group_created = false;
static std::unordered_set<std::string> tracked_rails;

void esp_handle_send_end(void *req_ptr)
{
	ESP_BEGIN_BATCH_REPORT()

	nccl_net_ofi_rdma_req *req = (nccl_net_ofi_rdma_req *)req_ptr;

	uint64_t start_ticks = req->req_start_time;
	if (start_ticks > 0) {
		ESP_ASYNC_HISTOGRAM_SAMPLE(ofi_tx_latency,
					   ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000),
					   true,
					   true,
					   "ns",
					   1,
					   ESP_CURR_TICK_COUNT - start_ticks);
		req->req_start_time = 0;
	}

	// track also time from last rail arrival to here (this is synchronous histogram)
	ESP_SYNC_HISTORGRAM_END_TIME(ofi_last_rail_handle_latency,
		ESP_DEFAULT_BIN_GENERATOR(), true, ESP_CURR_TICK_COUNT);

	ESP_END_BATCH_REPORT()
}

void esp_handle_write(void *req_ptr, void *recv_data_ptr, uint8_t recv_idx, uint64_t total_segms)
{
	nccl_net_ofi_rdma_req *req = (nccl_net_ofi_rdma_req *)req_ptr;
	rdma_recv_req *recv_data = (rdma_recv_req *)recv_data_ptr;

	// collect the following metrics:
	// 1. time from send till first segment arrival
	// 2. time from first to last segment (in each recv_idx) - segment assembly latency
	// 3. number of segments in recv part and segment size
	// 4. time from first segment of first recv part till last segment of last recv part
	bool first_part = req->ncompls == 0;
	// what if there are multiple proxy threads? we need some atomic check
	if ((first_part &&
	     recv_data->recvs[recv_idx].ncompls == 1) || // case 1, case 4 first segment
	    (recv_data->recvs[recv_idx].ncompls == 1) || // case 2 first segment
	    (recv_data->recvs[recv_idx].ncompls ==
	     (int)total_segms)) { // case 2 last segment, case 3
		uint64_t curr_ticks = ESP_GET_WALL_CLOCK();

		// case 1: first segment arrival
		if (first_part && recv_data->recvs[recv_idx].ncompls == 1) {
			uint64_t start_ticks = req->req_start_time;
			if (start_ticks > 0) {
				ESP_ASYNC_HISTOGRAM_SAMPLE(
					ofi_first_segment_delay,
					ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000),
					true,
					true,
					"ns",
					1,
					curr_ticks - start_ticks);
			}

			// case 4: save start time (matched when last part is assembled)
			req->req_assembly_start_time = curr_ticks;
		}

		// case 2: first segment in each part
		if (recv_data->recvs[recv_idx].ncompls == 1) {
			recv_data->recvs[recv_idx].start_ticks = curr_ticks;
		}
		// case 2: last segment in part
		if (recv_data->recvs[recv_idx].ncompls == (int)total_segms) {
			uint64_t start_ticks = recv_data->recvs[recv_idx].start_ticks;
			if (start_ticks > 0) {
				if (curr_ticks > start_ticks) {
					ESP_ASYNC_HISTOGRAM_SAMPLE(
						ofi_segment_assembly_latency,
						ESP_LINEAR_BIN_GENERATOR(500, 30, 0),
						true,
						true,
						"ns",
						1,
						curr_ticks - start_ticks);
				}
				recv_data->recvs[recv_idx].start_ticks = 0;
			}

			// case 3: segment count in part
			ESP_ASYNC_HISTOGRAM_SAMPLE(ofi_segment_count_in_part,
						   ESP_DEFAULT_BIN_GENERATOR(),
						   false,
						   true,
						   "",
						   1,
						   total_segms);

			// segment size (each "slice" is a WQE, and we have its size from libfabric
			// profiling)
			ESP_ASYNC_HISTOGRAM_SAMPLE(ofi_segment_size,
						   ESP_LOG2_BIN_GENERATOR(),
						   false,
						   true,
						   "bytes",
						   1,
						   recv_data->recvs[recv_idx].recv_size);
		}
	}
}

// profile gap between first and last completion of request parts
// (as an indicator for netowrk congestion),
// and gap between last rail completion to full handling (plugin latency)
void esp_handle_req_complete(void *req_ptr, int ncompls, int total_ncompls)
{
	nccl_net_ofi_rdma_req *req = (nccl_net_ofi_rdma_req *)req_ptr;

	// take time stamp only once and only when needed
	nccl_net_ofi_rdma_req_type_t req_type = req->get_type();
	if (req_type == NCCL_OFI_RDMA_SEND || req_type == NCCL_OFI_RDMA_WRITE) {
		uint64_t curr_ticks = 0;
		if ((total_ncompls == ncompls) || ((total_ncompls > 1) && (ncompls == 1)) ||
		    (ncompls == 1)) {
			curr_ticks = ESP_GET_WALL_CLOCK();
		}
		if (ncompls == 1) {
			uint64_t start_ticks = req->req_start_time;
			if (start_ticks > 0) {
				ESP_ASYNC_HISTOGRAM_SAMPLE(
					ofi_send_to_first_req_arrival,
					ESP_LINEAR_BIN_GENERATOR(20000, 30, 20000),
					true,
					true,
					"ns",
					1,
					curr_ticks - start_ticks);
			}
		}
		if (total_ncompls > 1) {
			// we do this only for multi-part/rail requests
			if (ncompls == 1) {
				// first part arrived, so record the time
				assert(curr_ticks != 0);
				req->req_first_rail_time = curr_ticks;
			} else if (ncompls == total_ncompls) {
				assert(curr_ticks != 0);
				uint64_t start_ticks = req->req_first_rail_time;
				if (start_ticks > 0) {
					if (req_type == NCCL_OFI_RDMA_SEND) {
						ESP_ASYNC_HISTOGRAM_SAMPLE(
							ofi_send_req_count,
							ESP_DEFAULT_BIN_GENERATOR(),
							false,
							true,
							"",
							1,
							ncompls);
						ESP_ASYNC_HISTOGRAM_SAMPLE(
							ofi_first_to_last_rail_send_latency,
							ESP_LINEAR_BIN_GENERATOR(500, 30, 0),
							true,
							true,
							"ns",
							1,
							curr_ticks - start_ticks);
					} else if (req_type == NCCL_OFI_RDMA_WRITE) {
						ESP_ASYNC_HISTOGRAM_SAMPLE(
							ofi_first_to_last_rail_write_latency,
							ESP_LINEAR_BIN_GENERATOR(500, 30, 0),
							true,
							true,
							"ns",
							1,
							curr_ticks - start_ticks);
					}
					req->req_first_rail_time = 0;
				}
			}
			// TODO: not relating to request type here (send/recv/write/etc.)
		}
		if (total_ncompls == ncompls && req_type == NCCL_OFI_RDMA_SEND) {
			// also track the time from here until request is fully handled
			// (both for single-rail and multi-rail requests)
			assert(curr_ticks != 0);
			ESP_SYNC_HISTORGRAM_START_TIME(ofi_last_rail_handle_latency,
						       ESP_DEFAULT_BIN_GENERATOR(),
						       true,
						       curr_ticks);
		}
	}
}

void esp_handle_request_assembly(void *req_ptr)
{
	nccl_net_ofi_rdma_req *req = (nccl_net_ofi_rdma_req *)req_ptr;

	uint64_t start_time = req->req_assembly_start_time;
	if (start_time > 0) {
		uint64_t curr_ticks = ESP_GET_WALL_CLOCK();
		ESP_ASYNC_HISTOGRAM_SAMPLE(ofi_req_assembly_latency,
					   ESP_LINEAR_BIN_GENERATOR(500, 30, 0),
					   true,
					   true,
					   "ns",
					   1,
					   curr_ticks - start_time);
		req->req_assembly_start_time = 0;
	}
}

void esp_track_rail_bw(int dev_id, nccl_net_ofi_device_t *device)
{
	// create group only once (we have no guarantee this is not multi-threaded so atomics are
	// used here)
	std::unique_lock<std::mutex> lock(track_rail_lock);
	const char *sendName = "Write BW";
	const char *recvName = "Read BW";
	if (!rate_var_group_created) {
		// divide by 8 to get Gbps
		esp::createRateVarGroup(sendName, 0, "Gbps", 1'000'000'000 / 8);
		esp::createRateVarGroup(recvName, 0, "Gbps", 1'000'000'000 / 8);
		rate_var_group_created = true;
	}

	auto *rdma_dev = static_cast<nccl_net_ofi_rdma_device_t *>(device);
	NCCL_OFI_INFO(NCCL_INIT,
		      "nccl_net_ofi_listen(): seeing %hu rails at %p",
		      rdma_dev->num_rails,
		      rdma_dev);
	for (int i = 0; i < rdma_dev->num_rails; i++) {
		struct fi_info *info = rdma_dev->rdma_device_get_rail(i)->info;
		std::string device_name = info->domain_attr->name;
		device_name = device_name.substr(0, device_name.find('-'));
		if (!tracked_rails.insert(device_name).second) {
			// rail already tracked, back off
			continue;
		}
		std::string writeFilePath = std::string("/sys/class/infiniband/") + device_name +
					    "/ports/1/hw_counters/rdma_write_bytes";
		std::string readFilePath = std::string("/sys/class/infiniband/") + device_name +
					   "/ports/1/hw_counters/rdma_read_bytes";
		esp::addRateVarGroupFileReader(sendName, writeFilePath.c_str());
		esp::addRateVarGroupFileReader(recvName, readFilePath.c_str());
		NCCL_OFI_INFO(NCCL_INIT,
			      "dev_id %d rail %d: %s, added group counter for %s",
			      dev_id,
			      i,
			      device_name.c_str(),
			      writeFilePath.c_str());
		NCCL_OFI_INFO(NCCL_INIT,
			      "dev_id %d rail %d: %s, added group counter for %s",
			      dev_id,
			      i,
			      device_name.c_str(),
			      readFilePath.c_str());
	}
}

#endif // HAVE_LIBESP == 1
