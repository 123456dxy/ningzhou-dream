# 用脚本输入驱动控制台界面，验证实际可玩性。
# 用法：.\tools\playtest.ps1
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$Exe = Join-Path $Root 'build\ningzhou.exe'

# 建号输入：姓名 / 性别 / 天赋 / 种子
$newGame = @(
    '测试者',      # 姓名
    '1',           # 性别 女
    '1',           # 天赋编号 1（sword）
    '',            # 种子（默认）
    '',            # 建号完成回车
    '3',           # 菜单：切换月度计划
    '1',           # 选闭关修炼
    '1',           # 推进行动
    '', '', '',    # 处理可能出现的抉择（回车默认第 1 项）
    '0',           # 退出
    '0'            # 不存档
)

Write-Host "=== 建号并推进一次行动 ===" -ForegroundColor Cyan
$newGame -join "`n" | & $Exe 2>&1 | Select-Object -First 60
