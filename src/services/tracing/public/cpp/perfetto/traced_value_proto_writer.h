// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_TRACING_PUBLIC_CPP_PERFETTO_TRACED_VALUE_PROTO_WRITER_H_
#define SERVICES_TRACING_PUBLIC_CPP_PERFETTO_TRACED_VALUE_PROTO_WRITER_H_

#include "base/component_export.h"
#include "base/trace_event/trace_event_impl.h"
#include "third_party/perfetto/include/perfetto/protozero/contiguous_memory_range.h"

namespace perfetto {
namespace protos {
namespace pbzero {
class ChromeTraceEvent_Arg;
}  // namespace pbzero
}  // namespace protos
}  // namespace perfetto

namespace tracing {

class COMPONENT_EXPORT(TRACING_CPP) PerfettoProtoAppender
    : public base::trace_event::ConvertableToTraceFormat::ProtoAppender {
 public:
  explicit PerfettoProtoAppender(
      perfetto::protos::pbzero::ChromeTraceEvent_Arg* proto);
  ~PerfettoProtoAppender() override;

  // ProtoAppender implementation
  void AddBuffer(uint8_t* begin, uint8_t* end) override;
  size_t Finalize(uint32_t field_id) override;

 private:
  std::vector<protozero::ContiguousMemoryRange> ranges_;
  perfetto::protos::pbzero::ChromeTraceEvent_Arg* proto_;
};

void COMPONENT_EXPORT(TRACING_CPP) RegisterTracedValueProtoWriter(bool enable);
}  // namespace tracing

#endif  // SERVICES_TRACING_PUBLIC_CPP_PERFETTO_TRACED_VALUE_PROTO_WRITER_H_
