// Backward stacktrace integration unit
#ifndef BACKWARD_DISABLE
#include <backward.hpp>

namespace backward {
// Provide a global signal handler instance
backward::SignalHandling sh;
}
#endif
