# SysMate

A lightweight Windows system information utility built with the Win32 SDK.

![SysMate screenshot](screenshot.png)

## Features

### Performance
- Real-time charts for CPU, Memory, Disk, Network, GPU, and Handle counts
- 60-sample scrolling history updated every second
- Anti-aliased line rendering with filled area graphs
- Responsive grid layout (2 columns wide, 1 column narrow)

### Processes
- Process list with name, PID, and working-set memory (Toolhelp32)
- Module list for the selected process showing path and size
- Filter-as-you-type search
- Context menu to show in Explorer or kill a process

### Network
- Active TCP and UDP connections (IP Helper API)
- Protocol, local/remote address, state, and owning PID

### Windows
- Enumeration of visible top-level windows with title, class, PID, and handle
- Detail pane showing rect, style, and extended style for the selected window
- Filter-as-you-type search
- Close window or open owning process in Explorer

## Building

Requires Visual Studio 2022 17.14+ (v145 toolset) or Visual Studio 2026.

```
msbuild src\SysMate.vcxproj /p:Configuration=Release /p:Platform=x64
```

Or open `SysMate.sln` in Visual Studio.

## History

Originally written circa 2001–2002 as an MFC/COM application for Visual C++ 6.0. Modernized to pure Win32 SDK, Unicode, and x64.

## License

Copyright (c) 2002+ Zac Walker
