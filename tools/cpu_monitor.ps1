Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# 2026-08-25: was an exact "FirstSynth" match, which stopped finding anything
# once Debug/Release Standalone builds got split into differently-named exes
# (FirstSynth_x64.exe / FirstSynth_x64_Release.exe, see progress.md) - the
# actual Windows process name always matches whichever exe filename was
# launched. Wildcard pattern catches both those and the original bare
# "FirstSynth" (e.g. running the raw build-win\app\x64\*\FirstSynth.exe
# output directly) without needing to know which one is currently running.
$procPattern = "FirstSynth*"
$coreCount = [Environment]::ProcessorCount

$form = New-Object System.Windows.Forms.Form
$form.Text = "CPU Monitor"
$form.Size = New-Object System.Drawing.Size(280, 150)
$form.TopMost = $true
$form.FormBorderStyle = "FixedToolWindow"
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point(20, 20)

$labelProc = New-Object System.Windows.Forms.Label
$labelProc.Text = "FirstSynth : (not running)"
$labelProc.Font = New-Object System.Drawing.Font("Consolas", 14, [System.Drawing.FontStyle]::Bold)
$labelProc.AutoSize = $true
$labelProc.Location = New-Object System.Drawing.Point(15, 15)
$form.Controls.Add($labelProc)

$labelSys = New-Object System.Windows.Forms.Label
$labelSys.Text = "System total : ..."
$labelSys.Font = New-Object System.Drawing.Font("Consolas", 11)
$labelSys.AutoSize = $true
$labelSys.Location = New-Object System.Drawing.Point(15, 55)
$form.Controls.Add($labelSys)

$labelCores = New-Object System.Windows.Forms.Label
$labelCores.Text = "Logical cores: $coreCount"
$labelCores.Font = New-Object System.Drawing.Font("Consolas", 9)
$labelCores.AutoSize = $true
$labelCores.Location = New-Object System.Drawing.Point(15, 90)
$form.Controls.Add($labelCores)

$script:lastCpuTime = $null
$script:lastSampleTime = $null
$script:lastPid = $null
$sysCounter = New-Object System.Diagnostics.PerformanceCounter("Processor", "% Processor Time", "_Total")
$sysCounter.NextValue() | Out-Null

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    # picks whichever matching process is actually running right now (Debug's
    # FirstSynth_x64, Release's FirstSynth_x64_Release, or a raw build-win\
    # app\x64\*\FirstSynth.exe) - only one can run at a time in practice
    # (some kind of single-instance/audio-device lock), but -First 1 keeps
    # this from erroring if that ever isn't true.
    $proc = Get-Process -Name $procPattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
        $now = Get-Date
        $cpuTime = $proc.TotalProcessorTime.TotalSeconds
        # reset the running delta if the matched process changed (e.g. Debug
        # closed and Release opened between ticks) - comparing CPU time across
        # two unrelated processes would produce a meaningless/garbage percentage
        if ($script:lastCpuTime -ne $null -and $script:lastPid -eq $proc.Id) {
            $elapsed = ($now - $script:lastSampleTime).TotalSeconds
            $cpuDelta = $cpuTime - $script:lastCpuTime
            $pct = [math]::Round(($cpuDelta / $elapsed / $coreCount) * 100, 1)
            $labelProc.Text = "$($proc.ProcessName) : $pct %"
            $labelProc.ForeColor = if ($pct -gt 60) { [System.Drawing.Color]::Red } elseif ($pct -gt 30) { [System.Drawing.Color]::DarkOrange } else { [System.Drawing.Color]::DarkGreen }
        } else {
            $labelProc.Text = "$($proc.ProcessName) : ..."
            $labelProc.ForeColor = [System.Drawing.Color]::Gray
        }
        $script:lastCpuTime = $cpuTime
        $script:lastSampleTime = $now
        $script:lastPid = $proc.Id
    } else {
        $labelProc.Text = "FirstSynth : (not running)"
        $labelProc.ForeColor = [System.Drawing.Color]::Gray
        $script:lastCpuTime = $null
        $script:lastPid = $null
    }

    $sysPct = [math]::Round($sysCounter.NextValue(), 1)
    $labelSys.Text = "System total : $sysPct %"
})
$timer.Start()

[System.Windows.Forms.Application]::Run($form)
