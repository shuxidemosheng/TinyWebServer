#!/bin/bash
# log_bench.sh —— 改造3基准: 不同日志配置下的 QPS 对比
# 用法: 在 TinyWebServer 仓库根目录执行  bash test_pressure/log_bench.sh
# 依赖: wrk, 运行中的 MySQL(qgydb), 已编译的 ./server
# 说明: 每组用例重启服务器; 日志文件在结束统一清理(日志量大, 按行数自动轮转)

cd "$(dirname "$0")/.." || exit 1

run_case() {
    local name="$1"; shift
    pkill -x server 2>/dev/null
    sleep 1
    ( setsid env TWS_DB_USER=appuser TWS_DB_PASS="App@123456" \
        ./server -p 8888 -m 0 "$@" >server_console.log 2>&1 & )
    sleep 2
    echo "--- $name ---"
    wrk -c 100 -t 2 -d 5s --timeout 3s http://127.0.0.1:8888/ 2>&1 \
        | grep -E "Requests/sec|Socket errors" \
        || echo "wrk 异常"
}

echo "日志基准矩阵开始: $(date '+%H:%M:%S')"
run_case "A 无日志 (close_log=1)"        -c 1
run_case "B 同步日志 (close_log=0)"      -c 0
run_case "C 异步日志 (close_log=0 -l 1)" -c 0 -l 1
pkill -x server 2>/dev/null
rm -f 2026_*ServerLog*
echo "矩阵结束: $(date '+%H:%M:%S')"
