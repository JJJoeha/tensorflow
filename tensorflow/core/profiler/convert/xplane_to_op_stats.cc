/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/profiler/convert/xplane_to_op_stats.h"

#include "tensorflow/core/platform/types.h"
#include "tensorflow/core/profiler/convert/xplane_to_op_metrics_db.h"
#include "tensorflow/core/profiler/protobuf/hardware_types.pb.h"
#include "tensorflow/core/profiler/utils/hardware_type_utils.h"
#include "tensorflow/core/profiler/utils/xplane_schema.h"
#include "tensorflow/core/profiler/utils/xplane_utils.h"
#include "tensorflow/core/profiler/utils/xplane_visitor.h"

namespace tensorflow {
namespace profiler {
namespace {

DeviceCapabilities GetDeviceCapFromXPlane(const XPlane& device_plane) {
  DeviceCapabilities cap;
  XPlaneVisitor plane(&device_plane);
  if (auto clock_rate_khz = plane.GetStats(kDevCapClockRateKHz)) {
    cap.set_clock_rate_in_ghz(clock_rate_khz->int64_value() / 1000000.0);
  }
  if (auto core_count = plane.GetStats(kDevCapCoreCount)) {
    cap.set_num_cores(core_count->int64_value());
  }
  // Set memory bandwidth in bytes/s.
  if (auto memory_bw = plane.GetStats(kDevCapMemoryBandwidth)) {
    cap.set_memory_bandwidth(memory_bw->int64_value());
  }
  if (auto memory_size_in_bytes = plane.GetStats(kDevCapMemorySize)) {
    cap.set_memory_size_in_bytes(memory_size_in_bytes->uint64_value());
  }
  if (auto cap_major = plane.GetStats(kDevCapComputeCapMajor)) {
    cap.mutable_compute_capability()->set_major(cap_major->int64_value());
  }
  if (auto cap_minor = plane.GetStats(kDevCapComputeCapMinor)) {
    cap.mutable_compute_capability()->set_minor(cap_minor->int64_value());
  }
  return cap;
}

PerfEnv GetPerfEnvFromXPlane(const XPlane& device_plane) {
  PerfEnv result;
  DeviceCapabilities cap = GetDeviceCapFromXPlane(device_plane);
  result.set_peak_tera_flops_per_second(GetFlopMaxThroughputPerSM(cap) / 1000 *
                                        cap.num_cores());
  result.set_peak_hbm_bw_giga_bytes_per_second(cap.memory_bandwidth() / 1e9);
  result.set_ridge_point(result.peak_tera_flops_per_second() * 1000 /
                         result.peak_hbm_bw_giga_bytes_per_second());
  return result;
}

}  // namespace

OpStats ConvertXSpaceToOpStats(const XSpace& space) {
  OpStats op_stats;
  // Hosts.
  if (const XPlane* host_trace = FindPlaneWithName(space, kHostThreads)) {
    *op_stats.mutable_host_op_metrics_db() =
        ConvertHostThreadsXPlaneToOpMetricsDb(*host_trace);
  }
  // Device.
  OpMetricsDbCombiner op_metrics_db_combiner(
      op_stats.mutable_device_op_metrics_db());
  for (const XPlane* device_trace :
       FindPlanesWithPrefix(space, kGpuPlanePrefix)) {
    if (!op_stats.has_perf_env()) {
      *op_stats.mutable_perf_env() = GetPerfEnvFromXPlane(*device_trace);
    }
    const PerfEnv& perf_env = op_stats.perf_env();
    OpMetricsDb device_op_metrics_db = ConvertDeviceTraceXPlaneToOpMetricsDb(
        *device_trace, perf_env.peak_tera_flops_per_second(),
        perf_env.peak_hbm_bw_giga_bytes_per_second());
    op_metrics_db_combiner.Combine(device_op_metrics_db);
  }
  return op_stats;
}

}  // namespace profiler
}  // namespace tensorflow
