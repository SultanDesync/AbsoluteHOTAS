# setup-env.ps1
# Configures the current PowerShell session with local developer environment variables
# from CMakeUserPresets.json. Copy CMakeUserPresets.example.json to
# CMakeUserPresets.json, then edit the paths for your machine.

$presetFile = Join-Path $PSScriptRoot "CMakeUserPresets.json"

if (Test-Path $presetFile) {
    try {
        $presets = Get-Content $presetFile -Raw | ConvertFrom-Json
        $userBase = $presets.configurePresets | Where-Object { $_.name -eq "user-base" }
        
        if ($userBase -and $userBase.environment) {
            Write-Host "Configuring MSVC/vcpkg developer environment..." -ForegroundColor Cyan
            
            $envMap = $userBase.environment
            foreach ($property in $envMap.psobject.properties) {
                $key = $property.Name
                $val = $property.Value

                try {
                    # Expand path placeholders like $penv{PATH}
                    if ($val -like "*`$penv{*}*") {
                        $matches = [regex]::Match($val, '\$penv\{([^}]+)\}')
                        if ($matches.Success) {
                            $penvName = $matches.Groups[1].Value
                            $origVal = Get-Item "env:$penvName" -ErrorAction SilentlyContinue
                            $origValStr = if ($origVal) { $origVal.Value } else { "" }
                            $val = $val -replace '\$penv\{[^}]+\}', $origValStr
                        }
                    }

                    # Replace forward slashes with backward slashes for Windows compatibility
                    $val = $val -replace '/', '\'

                    # Set in the current PowerShell session. Use direct assignments for
                    # common Windows variables to avoid Path/PATH duplicate-key issues.
                    switch ($key.ToUpperInvariant()) {
                        "PATH" { [System.Environment]::SetEnvironmentVariable("Path", $val, [System.EnvironmentVariableTarget]::Process); break }
                        "INCLUDE" { $env:INCLUDE = $val; break }
                        "LIB" { $env:LIB = $val; break }
                        "VCPKG_ROOT" { $env:VCPKG_ROOT = $val; break }
                        default { Set-Item -Path "Env:$key" -Value $val }
                    }
                    Write-Host "  [+] $key set successfully." -ForegroundColor Green
                } catch {
                    Write-Warning "Could not set $key; continuing. $($_.Exception.Message)"
                }
            }
            
            Write-Host "Developer environment variables are successfully imported!" -ForegroundColor Green
            Write-Host "You can now run 'cmake --build build/release' or 'ninja' directly in this window." -ForegroundColor Cyan
        } else {
            Write-Error "Could not find 'user-base' preset or its 'environment' block in CMakeUserPresets.json."
        }
    } catch {
        Write-Error "Failed to parse CMakeUserPresets.json: $_"
    }
} else {
    Write-Error "CMakeUserPresets.json not found at: $presetFile. Copy CMakeUserPresets.example.json to CMakeUserPresets.json and edit it for your machine."
}
