# TaskScheduler (C++23)

A high-performance single-host task scheduler skeleton. It supports task submission, queuing, dispatching, process isolation, resource quotas, timeouts, metrics export, optional persistence, Catch2 unit/benchmark tests, and backward-cpp stack traces.

## Highlights
- **Task lifecycle**: submit / queue / dispatch / run / timeout terminate / succeed / fail / cancel.
- **Resource quotas**: CPU & memory reservation/release to prevent oversubscription; optional cgroup v2 binding per job.
- **Scheduling**: priority (larger is higher) or FIFO; optional PSI backpressure (cgroup pressure files).
- **Isolation & timeout**: fork/exec per job, process-group SIGTERM → grace → SIGKILL two-phase timeout.
- **Observability**: Prometheus `/metrics`, `/health` endpoint, queue wait stats, backpressure counters.
- **Optional features**:
  - SQLite persistence for unfinished jobs (`ENABLE_PERSISTENCE`)
  - Simplified cron (`@every Ns`)
  - backward-cpp stack traces (`ENABLE_BACKWARD`)
- **Quality**: Catch2 unit test + enqueue throughput benchmark.

## Dependencies (Debian/Ubuntu example)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git \
  libdw-dev libbfd-dev libdwarf-dev libelf-dev \
  sqlite3 libsqlite3-dev      # optional for persistence
```
(Other distros: use the equivalent *-devel packages.)

## Get the code
```bash
git clone <repo-url> taskscheduler
cd taskscheduler
git submodule update --init --recursive
```

## Build configuration
Key CMake options:
- `-DENABLE_TESTS=ON|OFF`: build Catch2 tests and benchmarks (default ON).
- `-DENABLE_BACKWARD=ON|OFF`: enable backward-cpp stack traces (default ON).
- `-DENABLE_PERSISTENCE=ON|OFF`: enable SQLite persistence (default ON; falls back to stubs if SQLite missing).

Example:
```bash
cmake -S . -B build -DENABLE_TESTS=ON -DENABLE_BACKWARD=ON
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

## Stack traces (backward-cpp)
With libdw/libbfd/libdwarf present, uncaught exceptions or guarded threads print detailed stack traces. If missing, it falls back to header-only mode with reduced detail.

## Unit tests
```bash
ctest --test-dir build
# or run directly
./build/scheduler_tests
```
Current case: submit an `echo` job and wait for completion.

## Performance benchmark (Catch2)
```bash
./build/scheduler_bench
```
Current benchmark: `submit trivial echo`; output shows mean/stdev. The repeated “bench” lines are the tested command stdout—switch to `true` or redirect to `/dev/null` for silence.

## Key layout
- `src/`: core code (scheduler, resource_manager, metrics, cgroup_helper, cron_scheduler, job_store, logger, backward integration).
- `tests/`: Catch2 unit test and benchmark.
- `external/`: submodules Catch2 and backward-cpp.

## License
Apache-2.0
