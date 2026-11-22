# verify_win.ps1 — TBENCH-WIN-1 Windows 一键验证 (TBENCH-WIN-1 WP4)
#
# 用法 (在 gamma-p1-testbench 目录, x64 Native Tools Command Prompt):
#   powershell -ExecutionPolicy Bypass -File verify_win.ps1
#
# 步骤:
#   1) cmake 配置 + 构建 (Release, GP1_TESTS=ON), 全程日志 build_win.log,
#      从中提取告警行存 warnings_win.txt
#   2) ctest 跑 13 件, 输出存 ctest_win.log
#   3) win_render_dump 产 Windows 渲染基线 tests/baseline_win/render_dump_win.csv,
#      与 Mac 参考 (tests/baseline_win/mac_ref/render_dump.csv) 做 1e-6
#      容差逐样本比对 -> tests/baseline_win/compare_report.txt
#   4) 汇总 PASS/FAIL 打屏; 全部产物在 tests/baseline_win/ 供回传
#
# 注意: GUI 实跑 (W6 截图 / W7 Ctrl+C 日志闭环) 需人工按 README_WIN.md
# 第 4~5 节执行, 本脚本不代做。

param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$step = 0
$overallFail = 0

function Step($name) {
    $script:step++
    Write-Host ""
    Write-Host "==== [$script:step] $name ====" -ForegroundColor Cyan
}

# ---------- 1) 配置 + 构建 ----------
Step "cmake 配置 (Release, GP1_TESTS=ON)"
& cmake -B $BuildDir -A x64 -DGP1_TESTS=ON *>&1 | Tee-Object -FilePath build_win.log | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) { Write-Host "配置失败"; exit 1 }

Step "cmake 构建"
& cmake --build $BuildDir --config Release *>&1 | Tee-Object -FilePath build_win.log -Append | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) { Write-Host "构建失败"; exit 1 }

# 告警提取 (MSVC 格式: ... warning Cxxxx: ...)
Select-String -Path build_win.log -Pattern "warning" |
    ForEach-Object { $_.Line } | Out-File -Encoding utf8 warnings_win.txt
$warnCount = (Get-Content warnings_win.txt | Measure-Object -Line).Lines
Write-Host ("构建完成; 告警行数: {0} (目标 0, 非 0 请原样回传 warnings_win.txt)" -f $warnCount)
if ($warnCount -gt 0) { $overallFail = 1 }

# ---------- 2) 13 件测试 ----------
Step "ctest 13 件"
Push-Location $BuildDir
& ctest -C Release --output-on-failure *>&1 | Tee-Object -FilePath ..\ctest_win.log
$ctestRc = $LASTEXITCODE
Pop-Location
if ($ctestRc -ne 0) { Write-Host "ctest 有失败"; $overallFail = 1 }

# ---------- 3) 渲染基线容差比对 ----------
Step "渲染基线容差比对 (阈值 1e-6)"
$baseDir = Join-Path $root "tests\baseline_win"
$macRef  = Join-Path $baseDir "mac_ref\render_dump.csv"
$winOut  = Join-Path $baseDir "render_dump_win.csv"
$report  = Join-Path $baseDir "compare_report.txt"
& (Join-Path $root "$BuildDir\Release\win_render_dump.exe") $winOut
if ($LASTEXITCODE -ne 0) { Write-Host "win_render_dump 失败"; $overallFail = 1 }

"TBENCH-WIN-1 W8 基线容差比对报告" | Out-File -Encoding utf8 $report
("时间: {0}" -f (Get-Date)) | Out-File -Encoding utf8 -Append $report
("阈值: 1e-6 (逐样本 |win-mac|)" ) | Out-File -Encoding utf8 -Append $report
"" | Out-File -Encoding utf8 -Append $report

if (Test-Path $macRef) {
    $mac = Get-Content $macRef | Where-Object { $_ -notmatch '^#' }
    $win = Get-Content $winOut | Where-Object { $_ -notmatch '^#' }
    $n = [Math]::Min($mac.Count, $win.Count)
    $maxDiff = 0.0
    $badIdx = New-Object System.Collections.Generic.List[int]
    if ($mac.Count -ne $win.Count) {
        ("样本数不一致: mac={0} win={1}" -f $mac.Count, $win.Count) |
            Out-File -Encoding utf8 -Append $report
        $overallFail = 1
    }
    for ($i = 0; $i -lt $n; $i++) {
        $d = [Math]::Abs([double]$win[$i] - [double]$mac[$i])
        if ($d -gt $maxDiff) { $maxDiff = $d }
        if ($d -gt 1e-6) {
            if ($badIdx.Count -lt 20) { $badIdx.Add($i) }
        }
    }
    ("比对样本数: {0}" -f $n) | Out-File -Encoding utf8 -Append $report
    ("最大绝对差: {0:e3}" -f $maxDiff) | Out-File -Encoding utf8 -Append $report
    if ($badIdx.Count -eq 0 -and $mac.Count -eq $win.Count) {
        "结论: 全部 <= 1e-6, PASS" | Out-File -Encoding utf8 -Append $report
    } else {
        "结论: 超出阈值样本数 (前 20 索引): $($badIdx -join ', ')" |
            Out-File -Encoding utf8 -Append $report
        $overallFail = 1
    }
} else {
    "缺 Mac 参考 $macRef" | Out-File -Encoding utf8 -Append $report
    $overallFail = 1
}
Get-Content $report

# ---------- 4) 汇总 ----------
Step "汇总"
Write-Host "构建日志: build_win.log / 告警: warnings_win.txt / 测试: ctest_win.log"
Write-Host "基线产物: tests\baseline_win\"
if ($overallFail -eq 0) {
    Write-Host "== TBENCH-WIN-1 一键验证: 全绿 ==" -ForegroundColor Green
    exit 0
} else {
    Write-Host "== TBENCH-WIN-1 一键验证: 存在失败项, 见上述日志 ==" -ForegroundColor Red
    exit 1
}
