# TaskScheduler (C++23)

A high-performance single-host task scheduler skeleton. It supports task submission, queuing, dispatching, process isolation, resource quotas, timeouts, metrics export, optional persistence, Catch2 unit/benchmark tests, and pluggable stack traces (C++23 `std::stacktrace` or backward-cpp).

## Highlights
- **Task lifecycle**: submit / queue / dispatch / run / timeout terminate / succeed / fail / cancel.
- **Resource quotas**: CPU & memory reservation/release to prevent oversubscription; optional cgroup v2 binding per job.
- **Scheduling**: priority (larger is higher) or FIFO; optional PSI backpressure (cgroup pressure files).
- **Isolation & timeout**: fork/exec per job, process-group SIGTERM → grace → SIGKILL two-phase timeout.
- **Observability**: Prometheus `/metrics`, `/health` endpoint, queue wait stats, backpressure counters; NanoLog async file logging (default `/tmp/taskscheduler.log`).
- **Optional features**:
  - SQLite persistence for unfinished jobs (`ENABLE_PERSISTENCE`)
  - Simplified cron (`@every Ns`)
  - Pluggable stack traces: `std::stacktrace` or backward-cpp (see *Stack traces*)
- **Quality**: Catch2 unit test + enqueue throughput benchmark.

## Dependencies (Debian/Ubuntu example)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git \
  sqlite3 libsqlite3-dev      # optional for persistence
```

Optional (only if you want a specific stacktrace backend):
- **backward-cpp** (better symbolization): install debug/symbol libs, e.g. `libdw-dev`, `libelf-dev` (and optionally `libbfd-dev`, `libdwarf-dev`).
- **C++23 `std::stacktrace`**: requires a libstdc++ build with stacktrace support; some systems need `libstdc++-<gccver>-dev` providing `libstdc++_libbacktrace`.

(Other distros: use the equivalent *-devel packages.)

## Get the code
```bash
git clone <repo-url> taskscheduler
cd taskscheduler
```
Vendored deps are under `external/` (Catch2, NanoLog, backward-cpp).

## Build configuration
Key CMake options:
- `-DENABLE_TESTS=ON|OFF`: build Catch2 tests and benchmarks (default ON).
- `-DENABLE_PERSISTENCE=ON|OFF`: enable SQLite persistence (default ON; falls back to stubs if SQLite missing).
- `-DENABLE_BACKWARD=ON|OFF`: allow backward-cpp backend (default ON).
- `-DTASKSCHEDULER_STACKTRACE_BACKEND=auto|stacktrace|backward|none`:
  - `auto` (default): use `std::stacktrace` if linkable; otherwise fall back to backward-cpp (if enabled).
  - `stacktrace`: force `std::stacktrace` (CMake will error if not linkable).
  - `backward`: force backward-cpp.
  - `none`: disable stack traces.

Example:
```bash
cmake -S . -B build \
  -DENABLE_TESTS=ON \
  -DTASKSCHEDULER_STACKTRACE_BACKEND=auto
cmake --build build -j
```

## Run example
```bash
./build/scheduler \
  --cmd "echo hello" \
  --cpu 1 --mem 256 --timeout 5 --priority 1 \
  --total-cpu 4 --total-mem 2048 \
  --metrics-port 8080 \
  --cgroup              # optional: enable cgroup limits
```
- /health: `curl http://localhost:8080/health`
- /metrics: `curl http://localhost:8080/metrics`

## Logging (NanoLog)
- Default log file: `/tmp/taskscheduler.log`. You can change it via `NanoLog::setLogFile("<path>")` before starting the scheduler.
- Use `NANO_LOG(level, "message %d", x);` with levels `DEBUG/INFO/NOTICE/WARNING/ERROR/CRIT` (via `using namespace NanoLog::LogLevels`).
- Preallocates buffers for throughput; ensure the target log directory is writable.

## Stack traces (std::stacktrace / backward-cpp)
This project supports two stacktrace backends selectable at CMake configure time:
- **C++23 `std::stacktrace`** (preferred when available)
- **backward-cpp** (fallback; can provide better symbolization if optional libs are installed)

### Selecting backend
Use `-DTASKSCHEDULER_STACKTRACE_BACKEND=auto|stacktrace|backward|none`.

### Notes about `std::stacktrace`
On some systems, `<stacktrace>` compiles but fails to link unless `libstdc++_libbacktrace` is present.
Quick checks:
```bash
# should print an absolute path if available
gcc -print-file-name=libstdc++_libbacktrace.a

# example compile+link check
cat >/tmp/stacktrace_check.cpp <<'EOF'
#include <stacktrace>
#include <iostream>
int main(){ std::cout << std::stacktrace::current() << "\n"; }
EOF
g++ -std=c++23 /tmp/stacktrace_check.cpp -o /tmp/stacktrace_check
```

### Notes about backward-cpp
If you install `libdw-dev`/`libelf-dev` (and optionally `libbfd-dev`/`libdwarf-dev`), stack traces may include richer symbols.

## Unit tests
```bash
ctest --test-dir build
# or run directly
./build/scheduler_tests
```
Current case: submit an `echo` job and wait for completion.

Note: tests initialize NanoLog lazily (via `std::call_once`) to avoid global static initialization order issues.

## Performance benchmark (Catch2)
```bash
./build/scheduler_bench
```
Current benchmark: `submit trivial echo`; output shows mean/stdev. The repeated “bench” lines are the tested command stdout—switch to `true` or redirect to `/dev/null` for silence.

## Key layout
- `src/`: core code (scheduler, resource_manager, metrics, cgroup_helper, cron_scheduler, job_store, NanoLog integration, stacktrace backends). Includes `nanolog_generated_stubs.cpp` for NanoLog's `GeneratedFunctions` symbol.
- `tests/`: Catch2 unit test and benchmark.
- `external/`: vendored Catch2, NanoLog, backward-cpp.

## License
Apache-2.0
