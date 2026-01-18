# TaskScheduler 接口说明

本文档汇总对外可见的接口与使用方式，包括命令行参数和 HTTP 端点。

## 1) 命令行接口（可执行文件 `scheduler`）
> 二进制路径通常为 `./build/scheduler`

| 参数 | 必填 | 说明 | 默认值 |
| --- | --- | --- | --- |
| `--cmd <string>` | 否 | 要执行的命令；不传则仅启动服务与指标 | 无 |
| `--cpu <int>` | 否 | 任务所需 CPU 核数 | 1 |
| `--mem <int>` | 否 | 任务所需内存（MB） | 256 |
| `--timeout <int>` | 否 | 任务超时（秒，0 表示不超时） | 0 |
| `--priority <int>` | 否 | 任务优先级（大者先） | 0 |
| `--total-cpu <int>` | 否 | 调度器全局可用 CPU | 4 |
| `--total-mem <int>` | 否 | 调度器全局可用内存（MB） | 2048 |
| `--cgroup` | 否 | 启用 cgroup v2 限制（基路径 `/sys/fs/cgroup/scheduler`） | 关 |
| `--enable-priority` | 否 | 开启优先级调度（否则 FIFO） | 关 |
| `--metrics-port <int>` | 否 | 启动 HTTP `/metrics` 与 `/health` 端口 | 关（-1） |
| `--whitelist <a,b>` | 否 | 命令白名单（逗号分隔），非白名单拒绝 | 空 |
| `--blacklist <a,b>` | 否 | 命令黑名单（逗号分隔），命中则拒绝 | 空 |
| `--workdir <path>` | 否 | 任务工作目录 | 继承当前目录 |
| `--rlimit-nofile <int>` | 否 | 进程最大文件描述符数 | 不调整 |
| `--db-path <path>` | 否 | 启用 SQLite 持久化并指定 DB 路径 | `state/tasks.db`（若启用） |
| `--enable-cron` | 否 | 启用简易 cron 调度（@every Ns） | 关 |
| `--cron-tick-ms <int>` | 否 | cron 轮询周期（毫秒） | 1000 |

### 最简示例
```bash
./build/scheduler \
  --cmd "echo hello" \
  --cpu 1 --mem 256 --timeout 5 --priority 1 \
  --total-cpu 4 --total-mem 2048 \
  --metrics-port 8080
```

## 2) HTTP 接口（需指定 `--metrics-port`）
- 基础地址：`http://<host>:<port>`，端口为命令行指定的 `--metrics-port`。
- 文本内容均为 `text/plain`。

### 2.1 /health
- 方法：GET
- 响应：`200 OK`，正文 `ok\n`

### 2.2 /metrics
- 方法：GET
- 响应：`200 OK`，正文为 Prometheus 文本格式的指标快照。
- 指标覆盖：提交/拒绝/运行中的计数、排队长度、基础延迟等（详见运行时输出）。

## 3) 任务与调度行为摘要
- 任务模型：`JobSpec { cmd, cpu_cores, memory_mb, timeout_sec, priority }`。
- 生命周期：提交 → 排队 → 派发 → 运行 → 成功/失败/超时/取消；超时采用 SIGTERM→宽限→SIGKILL。
- 资源配额：全局 `total_cpu/total_mem_mb`；若启用 cgroup，会为每个任务创建子 cgroup 限制 CPU/内存。
- 调度策略：默认 FIFO，可通过 `--enable-priority` 改为优先级（数值越大越先执行）。
- 可选持久化：传入 `--db-path` 即启用 SQLite，保存未完成任务状态，重启后恢复。
- Cron：`--enable-cron` + 模板（代码内配置）支持 `@every Ns` 周期调度。

## 4) 日志
- 运行时尝试将日志写入：`/tmp/taskscheduler.log` → `./taskscheduler.log` → `/dev/null`（依次回退）。
- 日志级别默认 NOTICE，可在代码中调整；使用 NanoLog 异步输出。

## 5) 退出与健康
- 进程退出码：0 表示正常；非 0 表示运行时抛出未处理异常。
- `/health` 返回 200 代表主循环与 HTTP 线程仍存活。
