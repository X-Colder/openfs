#include "data/segment_sealer.h"
#include "common/logging.h"

namespace openfs
{

    Status SegmentSealer::SealSegment(SegmentWriter &writer)
    {
        auto s = writer.Seal();
        if (s != Status::kOk)
            LOG_ERROR("Failed to seal segment {}", writer.SegmentId());
        return s;
    }

} // namespace openfs