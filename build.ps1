param([string]$SokuLibRoot = "$PSScriptRoot/deps/SokuLib")
$ErrorActionPreference = 'Stop'
cmake -S $PSScriptRoot -B "$PSScriptRoot/build" -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_BUILD_TYPE=Release "-DSOKULIB_ROOT=$SokuLibRoot"
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build "$PSScriptRoot/build" --config Release --target swarmmod swarmmod_state_tests
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir "$PSScriptRoot/build" -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
$output = "$PSScriptRoot/dist/modules/SwarmMod"
New-Item -ItemType Directory -Force $output | Out-Null
Copy-Item "$PSScriptRoot/build/Release/SwarmMod.dll" "$output/SwarmMod.dll"
Copy-Item "$SokuLibRoot/LICENSE" "$output/SokuLib-LICENSE.txt"
Copy-Item "$PSScriptRoot/mod.json" "$output/mod.json"
Write-Host "Built $output/SwarmMod.dll"
