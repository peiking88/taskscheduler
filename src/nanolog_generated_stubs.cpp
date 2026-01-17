#include "GeneratedCode.h"

namespace GeneratedFunctions {
// Minimal stubs to satisfy NanoLog symbols when not using the preprocessor
// generated code path.
size_t numLogIds = 0;
LogMetadata logId2Metadata[1];
ssize_t (*compressFnArray[])(NanoLogInternal::Log::UncompressedEntry *re, char *out) = {nullptr};
void (*decompressAndPrintFnArray[])(const char **in, FILE *outputFd, void (*aggFn)(const char *, ...)) = {nullptr};
long int writeDictionary(char * /*buffer*/, char * /*endOfBuffer*/) { return 0; }
} // namespace GeneratedFunctions
