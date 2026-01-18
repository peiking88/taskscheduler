// Stacktrace backend: backward-cpp signal handling integration
//
// When TASKSCHEDULER_USE_BACKWARD is enabled, we install backward-cpp's
// signal handler to automatically print stack traces on crashes.

#if defined(TASKSCHEDULER_USE_BACKWARD) && TASKSCHEDULER_USE_BACKWARD
#include <backward.hpp>

namespace backward {
backward::SignalHandling sh;
} // namespace backward
#endif
