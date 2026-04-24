#pragma once

#include "common/types.h"
#include "data/segment_writer.h"

namespace openfs
{

    class SegmentSealer
    {
    public:
        SegmentSealer() = default;
        // Seal a segment writer when it's full
        Status SealSegment(SegmentWriter &writer);
    };

} // namespace openfs