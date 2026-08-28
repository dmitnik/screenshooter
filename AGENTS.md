# Project commands

Run all build commands from a Developer Command Prompt initialized by:

```powershell
cmd.exe /d /s /c '"D:\DevTools\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && <command>'
```

The project uses the portable CMake executable at:

```text
D:\DevTools\CMake\cmake-4.4.3-windows-x86_64\bin\cmake.exe
```

Configure, build, and test Debug:

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Configure, build, and test Release:

```text
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Do not run interactive share or capture spikes when no interactive desktop session
is available. Do not add generated build output from `out/` to Git.
