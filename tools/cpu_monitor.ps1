Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$procName = "FirstSynth"
$coreCount = [Environment]::ProcessorCount

$form = New-Object System.Windows.Forms.Form
$form.Text = "CPU Monitor"
$form.Size = New-Object System.Drawing.Size(280, 150)
$form.TopMost = $true
$form.FormBorderStyle = "FixedToolWindow"
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point(20, 20)

$labelProc = New-Object System.Windows.Forms.Label
$labelProc.Text = "$procName : (not running)"
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
$sysCounter = New-Object System.Diagnostics.PerformanceCounter("Processor", "% Processor Time", "_Total")
$sysCounter.NextValue() | Out-Null

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    $proc = Get-Process -Name $procName -ErrorAction SilentlyContinue
    if ($proc) {
        $now = Get-Date
        $cpuTime = $proc.TotalProcessorTime.TotalSeconds
        if ($script:lastCpuTime -ne $null) {
            $elapsed = ($now - $script:lastSampleTime).TotalSeconds
            $cpuDelta = $cpuTime - $script:lastCpuTime
            $pct = [math]::Round(($cpuDelta / $elapsed / $coreCount) * 100, 1)
            $labelProc.Text = "$procName : $pct %"
            $labelProc.ForeColor = if ($pct -gt 60) { [System.Drawing.Color]::Red } elseif ($pct -gt 30) { [System.Drawing.Color]::DarkOrange } else { [System.Drawing.Color]::DarkGreen }
        }
        $script:lastCpuTime = $cpuTime
        $script:lastSampleTime = $now
    } else {
        $labelProc.Text = "$procName : (not running)"
        $labelProc.ForeColor = [System.Drawing.Color]::Gray
        $script:lastCpuTime = $null
    }

    $sysPct = [math]::Round($sysCounter.NextValue(), 1)
    $labelSys.Text = "System total : $sysPct %"
})
$timer.Start()

[System.Windows.Forms.Application]::Run($form)
