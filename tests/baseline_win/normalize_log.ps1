# normalize_log.ps1 — TBENCH-WIN-1 W9 日志归一化 (格式 v1 冻结验证用)
#
# 归一规则 (与 Mac 样本 diff 前必须执行):
#   1. t_ms 列 (行首 8 字符宽度数字) -> 0
#   2. # session_start 行 -> 日期/编译戳抹平
#      (# session_start <日期> sr=48000 build=<编译期> 保留 sr= 后内容)
#   3. op=signal 行 target 平台字面 -> SIGNAL_TOKEN
#      (Mac "SIGINT|SIGTERM" / Windows "CTRL_C|CTRL_CLOSE", WP2c 规格)
#
# 用法: powershell -File normalize_log.ps1 <日志文件>

param(
    [Parameter(Mandatory=$true)][string]$Path
)

Get-Content $Path | ForEach-Object {
    $line = $_
    if ($line -match '^#\s+session_start\s') {
        if ($line -match '(sr=.*)$') { "# session_start <DATE> $($Matches[1])" }
        else { "# session_start <DATE>" }
    }
    elseif ($line -match '^\s*\d+\s+\|') {
        # t_ms 列归零 (行首空白+数字)
        $line = $line -replace '^\s*\d+', '0'
        # signal target 平台字面归一 (行结构/其余列不动)
        $line = $line -replace 'SIGINT\|SIGTERM', 'SIGNAL_TOKEN'
        $line = $line -replace 'CTRL_C\|CTRL_CLOSE', 'SIGNAL_TOKEN'
        $line
    }
    else { $line }
}
