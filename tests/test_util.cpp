#include "test_util.h"
#include "common/logging.h"

namespace openfs
{
    namespace test
    {
        void InitTestLogging()
        {
            static bool initialized = false;
            if (!initialized)
            {
                InitLogging("test");
                spdlog::set_level(spdlog::level::warn); // Suppress info/debug in tests
                initialized = true;
            }
        }
    } // namespace test
} // namespace openfs