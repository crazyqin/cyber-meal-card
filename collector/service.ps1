param([ValidateSet('install','start','stop','status','remove')][string]$Action = 'status')
$ErrorActionPreference = 'Stop'
$collectorRoot = $PSScriptRoot
$collectorTaskName = 'CyberMealCard-Collector'
switch ($Action) {
    'install' {
        $collectorPython = Join-Path $collectorRoot '.venv/Scripts/pythonw.exe'
        $collectorScript = Join-Path $collectorRoot 'supervise.py'
        $collectorConfig = Join-Path $collectorRoot 'config.local.json'
        foreach ($collectorPath in @($collectorPython,$collectorScript,$collectorConfig)) {
            if (-not (Test-Path -LiteralPath $collectorPath)) { throw 'Collector setup is incomplete.' }
        }
        $collectorUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
        $collectorAction = New-ScheduledTaskAction -Execute $collectorPython -Argument ('-X utf8 "' + $collectorScript + '" --config "' + $collectorConfig + '"') -WorkingDirectory $collectorRoot
        $collectorTrigger = New-ScheduledTaskTrigger -AtLogOn -User $collectorUser
        $collectorWatchdog = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(10) -RepetitionInterval (New-TimeSpan -Minutes 1)
        $collectorPrincipal = New-ScheduledTaskPrincipal -UserId $collectorUser -LogonType Interactive -RunLevel Limited
        $collectorSettings = New-ScheduledTaskSettingsSet -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
        Register-ScheduledTask -TaskName $collectorTaskName -Action $collectorAction -Trigger @($collectorTrigger,$collectorWatchdog) -Principal $collectorPrincipal -Settings $collectorSettings -Description 'Cyber Meal Card collector: login startup and automatic recovery.' -Force | Out-Null
        Start-ScheduledTask -TaskName $collectorTaskName
    }
    'start' {
        Enable-ScheduledTask -TaskName $collectorTaskName | Out-Null
        Start-ScheduledTask -TaskName $collectorTaskName
    }
    'stop' {
        Disable-ScheduledTask -TaskName $collectorTaskName | Out-Null
        Stop-ScheduledTask -TaskName $collectorTaskName
    }
    'remove' {
        Stop-ScheduledTask -TaskName $collectorTaskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $collectorTaskName -Confirm:$false
        return
    }
}
Get-ScheduledTask -TaskName $collectorTaskName | Select-Object TaskName,State
