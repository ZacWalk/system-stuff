#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <psapi.h>
#include <pdh.h>
#include <stdio.h>
#include <uxtheme.h>

#include <vector>
#include <string>
#include <algorithm>
#include <deque>

#include "resource.h"
#include "chart.h"
#include "layout.h"
#include "dark_theme.h"
#include "custom_controls.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls'" \
    " version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ============================================================
// Globals
// ============================================================
static HINSTANCE g_hInst;
static HWND g_hMainWnd;
static CustomTabControl g_tabCtrl;
static HWND g_hFilterEdit;
static HFONT g_hFont;
static HFONT g_hFontBold;
static HFONT g_hFontChart;
static HFONT g_hFontTitle;
static HFONT g_hFontSubtitle;
static int g_nCurrentTab;
static std::wstring g_filterText;

enum TabId { TAB_PERFORMANCE, TAB_PROCESSES, TAB_NETWORK, TAB_WINDOWS, TAB_ABOUT, TAB_COUNT };

static std::wstring g_tabFilterText[TAB_COUNT];

static const WCHAR* g_tabNames[] = {L"Performance", L"Processes", L"Network", L"Windows", L"About"};
static HWND g_hTabPages[TAB_COUNT];

// ============================================================
// Performance tab
// ============================================================
static HWND g_hPerfPanel;
static constexpr int NUM_CHARTS = 6;
static Chart g_charts[NUM_CHARTS];
static const wchar_t* g_chartTitles[] = {L"CPU", L"Memory", L"Disk", L"Network", L"GPU", L"Handles"};
static std::deque<float> g_chartData[NUM_CHARTS];
static constexpr int CHART_HISTORY = 60;
static UINT_PTR g_perfTimer = 0;

static DWORDLONG g_memTotalBytes = 0;
static DWORDLONG g_memAvailBytes = 0;
static float g_rawHandleCount = 0;
static float g_handleMaxSeen = 10000.f;

// Button bar
enum
{
	BTN_REFRESH_PROC = 2000, BTN_EXPLORER_PROC, BTN_KILL_PROC,
	BTN_REFRESH_NET,
	BTN_REFRESH_WIN, BTN_EXPLORER_WIN, BTN_KILL_WIN,
	BTN_REFRESH_ABOUT,
};

static HWND g_btnRefreshProc, g_btnExplorerProc, g_btnKillProc;
static HWND g_btnRefreshNet;
static HWND g_btnRefreshWin, g_btnExplorerWin, g_btnKillWin;
static HWND g_btnRefreshAbout;
static HWND g_hAboutPanel;

static PDH_HQUERY g_pdhQuery = nullptr;
static PDH_HCOUNTER g_pdhCpu = nullptr;
static PDH_HCOUNTER g_pdhDisk = nullptr;
static PDH_HCOUNTER g_pdhNet = nullptr;
static PDH_HCOUNTER g_pdhHandles = nullptr;

static void InitPerfCounters()
{
	PdhOpenQuery(nullptr, 0, &g_pdhQuery);
	if (g_pdhQuery)
	{
		PdhAddEnglishCounter(g_pdhQuery, L"\\Processor(_Total)\\% Processor Time", 0, &g_pdhCpu);
		PdhAddEnglishCounter(g_pdhQuery, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_pdhDisk);
		PdhAddEnglishCounter(g_pdhQuery, L"\\Network Interface(*)\\Bytes Total/sec", 0, &g_pdhNet);
		PdhAddEnglishCounter(g_pdhQuery, L"\\Process(_Total)\\Handle Count", 0, &g_pdhHandles);
		PdhCollectQueryData(g_pdhQuery);
	}
	for (int i = 0; i < NUM_CHARTS; i++)
	{
		g_charts[i].SetTitle(g_chartTitles[i]);
		ChartStyle s;
		switch (i)
		{
		case 0: s.lineColor = {0, 180, 255};
			s.fillColor = {0, 120, 200};
			break;
		case 1: s.lineColor = {180, 0, 255};
			s.fillColor = {140, 0, 200};
			break;
		case 2: s.lineColor = {0, 200, 100};
			s.fillColor = {0, 150, 80};
			break;
		case 3: s.lineColor = {255, 180, 0};
			s.fillColor = {200, 140, 0};
			break;
		case 4: s.lineColor = {255, 80, 80};
			s.fillColor = {200, 50, 50};
			break;
		case 5: s.lineColor = {100, 220, 220};
			s.fillColor = {60, 170, 170};
			break;
		}
		g_charts[i].SetStyle(s);
		g_chartData[i].resize(CHART_HISTORY, 0.f);
	}
}

static void PushChartSample(const int chartIdx, const float value)
{
	g_chartData[chartIdx].push_back(std::clamp(value, 0.f, 100.f));
	if (g_chartData[chartIdx].size() > CHART_HISTORY) g_chartData[chartIdx].pop_front();
}

static void SamplePerfData()
{
	if (!g_pdhQuery) return;
	PdhCollectQueryData(g_pdhQuery);

	PDH_FMT_COUNTERVALUE val;
	float cpu = 0;
	if (g_pdhCpu && PdhGetFormattedCounterValue(g_pdhCpu, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
		cpu = static_cast<float>(val.doubleValue);
	PushChartSample(0, cpu);

	MEMORYSTATUSEX ms = {sizeof(ms)};
	GlobalMemoryStatusEx(&ms);
	g_memTotalBytes = ms.ullTotalPhys;
	g_memAvailBytes = ms.ullAvailPhys;
	PushChartSample(1, static_cast<float>(ms.dwMemoryLoad));

	float disk = 0;
	if (g_pdhDisk && PdhGetFormattedCounterValue(g_pdhDisk, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
		disk = static_cast<float>(val.doubleValue);
	PushChartSample(2, disk);

	float net = 0;
	if (g_pdhNet && PdhGetFormattedCounterValue(g_pdhNet, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
		net = static_cast<float>(val.doubleValue / 100000000.0 * 100.0);
	PushChartSample(3, net);

	PushChartSample(4, 0.f);

	float handles = 0;
	if (g_pdhHandles && PdhGetFormattedCounterValue(g_pdhHandles, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
	{
		g_rawHandleCount = static_cast<float>(val.doubleValue);
		if (g_rawHandleCount > g_handleMaxSeen) g_handleMaxSeen = g_rawHandleCount * 1.2f;
		handles = (g_handleMaxSeen > 0) ? (g_rawHandleCount / g_handleMaxSeen * 100.f) : 0.f;
	}
	PushChartSample(5, handles);
}

static void RenderPerfCharts(const HWND hWnd, const HDC hdc)
{
	RECT rc;
	GetClientRect(hWnd, &rc);
	FillRect(hdc, &rc, Dark::BrushWindow());
	const int totalW = rc.right, totalH = rc.bottom;
	if (totalW <= 0 || totalH <= 0) return;

	const int pad = Dpi::Scale(Layout::ChartPadding);
	const int spacing = Dpi::Scale(Layout::ChartSpacing);
	const int cols = (totalW > Dpi::Scale(500)) ? 2 : 1;
	const int rows = (NUM_CHARTS + cols - 1) / cols;
	const int chartW = totalW / cols - spacing;
	const int chartH = totalH / rows - spacing;
	if (chartW < Dpi::Scale(50) || chartH < Dpi::Scale(30)) return;

	for (int i = 0; i < NUM_CHARTS; i++)
	{
		const int col = i % cols, row = i / cols;
		const int x = pad + col * (chartW + spacing);
		const int y = pad + row * (chartH + spacing);

		g_charts[i].Resize(chartW, chartH);
		g_charts[i].SetMargin(Dpi::Scale(Layout::ChartGridMargin));
		std::vector<float> data(g_chartData[i].begin(), g_chartData[i].end());
		g_charts[i].SetData(data, 0, 100);
		g_charts[i].Render();
		g_charts[i].Paint(hdc, x, y);

		const int textMargin = Dpi::Scale(Layout::ChartTextMargin);
		const int textTop = Dpi::Scale(Layout::ChartTextTop);
		const int textBottom = Dpi::Scale(Layout::ChartTextBottom);
		const int valRightMargin = Dpi::Scale(Layout::ChartValueRightMargin);

		SetBkMode(hdc, TRANSPARENT);
		SelectObject(hdc, g_hFontChart);
		SetTextColor(hdc, RGB(200, 200, 200));
		RECT textRc = {x + textMargin, y + textTop, x + chartW - spacing, y + textBottom};
		DrawTextExt(hdc, g_chartTitles[i], -1, textRc, TextAlign::Left);

		WCHAR valStr[64];
		const float latest = data.empty() ? 0.f : data.back();
		if (i == 5) // Handles: show count
			_snwprintf_s(valStr, _countof(valStr), _TRUNCATE, L"%d", static_cast<int>(g_rawHandleCount));
		else
			_snwprintf_s(valStr, _countof(valStr), _TRUNCATE, L"%.1f%%", latest);
		SetTextColor(hdc, RGB(255, 255, 255));
		RECT valRc = {x + chartW - valRightMargin, y + textTop, x + chartW - textMargin, y + textBottom};
		DrawTextExt(hdc, valStr, -1, valRc, TextAlign::Right);

		// Detail line for memory and handles
		if (i == 1 || i == 5)
		{
			const int detailBottom = Dpi::Scale(Layout::ChartDetailBottom);
			SelectObject(hdc, g_hFont);
			SetTextColor(hdc, RGB(160, 160, 160));
			WCHAR detail[96] = {};
			if (i == 1)
				_snwprintf_s(detail, _countof(detail), _TRUNCATE, L"Used %.1f / %.1f GB  (Available %.1f GB)",
				             (g_memTotalBytes - g_memAvailBytes) / (1024.0 * 1024.0 * 1024.0),
				             g_memTotalBytes / (1024.0 * 1024.0 * 1024.0),
				             g_memAvailBytes / (1024.0 * 1024.0 * 1024.0));
			else
				_snwprintf_s(detail, _countof(detail), _TRUNCATE, L"Total system handles");
			RECT detRc = {x + textMargin, y + textBottom, x + chartW - spacing, y + detailBottom};
			DrawTextExt(hdc, detail, -1, detRc, TextAlign::Left);
		}
	}
}

static LRESULT CALLBACK PerfPanelProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	switch (msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			RenderPerfCharts(hWnd, ps.hdc);
			EndPaint(hWnd, &ps);
			return 0;
		}
	case WM_ERASEBKGND: return 1;
	case WM_TIMER:
		SamplePerfData();
		InvalidateRect(hWnd, nullptr, FALSE);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// Process tab
// ============================================================
static CustomListView g_procList;
static CustomListView g_moduleList;

struct ProcessInfo
{
	DWORD pid;
	std::wstring name;
	std::wstring path;
	SIZE_T workingSet;
};

static std::vector<ProcessInfo> g_processes;

static bool MatchesFilter(const std::wstring& text)
{
	if (g_filterText.empty()) return true;
	std::wstring lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
	std::wstring filt = g_filterText;
	std::transform(filt.begin(), filt.end(), filt.begin(), towlower);
	return lower.find(filt) != std::wstring::npos;
}

static void RefreshProcessList()
{
	g_procList.SetRedraw(false);
	g_procList.DeleteAllItems();
	g_processes.clear();

	const HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap == INVALID_HANDLE_VALUE)
	{
		g_procList.SetRedraw(true);
		return;
	}

	PROCESSENTRY32W pe = {sizeof(pe)};
	if (Process32FirstW(hSnap, &pe))
	{
		do
		{
			if (pe.th32ProcessID == 0) continue;
			ProcessInfo pi;
			pi.pid = pe.th32ProcessID;
			pi.name = pe.szExeFile;
			pi.workingSet = 0;

			const HANDLE hProc = OpenProcess(
				PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
			if (hProc)
			{
				PROCESS_MEMORY_COUNTERS pmc = {};
				if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc)))
					pi.workingSet = pmc.WorkingSetSize;
				WCHAR path[MAX_PATH] = {};
				DWORD pathLen = MAX_PATH;
				if (QueryFullProcessImageNameW(hProc, 0, path, &pathLen))
					pi.path = path;
				CloseHandle(hProc);
			}

			if (!MatchesFilter(pi.name)) continue;

			g_processes.push_back(pi);
		}
		while (Process32NextW(hSnap, &pe));
	}
	CloseHandle(hSnap);

	std::sort(g_processes.begin(), g_processes.end(),
	          [](const ProcessInfo& a, const ProcessInfo& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });

	for (int i = 0; i < static_cast<int>(g_processes.size()); i++)
	{
		auto& pi = g_processes[i];
		const int idx = g_procList.AddItem(pi.name.c_str());
		g_procList.SetItemData(idx, pi.pid);

		WCHAR pidStr[16];
		_snwprintf_s(pidStr, _countof(pidStr), _TRUNCATE, L"%u", pi.pid);
		g_procList.SetItemText(idx, 1, pidStr);

		WCHAR memStr[32];
		_snwprintf_s(memStr, _countof(memStr), _TRUNCATE, L"%.1f MB", pi.workingSet / (1024.0 * 1024.0));
		g_procList.SetItemText(idx, 2, memStr);
	}

	g_procList.SetRedraw(true);
}

static void RefreshModuleList(const DWORD pid)
{
	g_moduleList.SetRedraw(false);
	g_moduleList.DeleteAllItems();

	const HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (hSnap == INVALID_HANDLE_VALUE)
	{
		g_moduleList.SetRedraw(true);
		return;
	}

	MODULEENTRY32W me = {sizeof(me)};
	if (Module32FirstW(hSnap, &me))
	{
		do
		{
			const int item = g_moduleList.AddItem(me.szModule);
			g_moduleList.SetItemText(item, 1, me.szExePath);
			WCHAR sizeStr[32];
			_snwprintf_s(sizeStr, _countof(sizeStr), _TRUNCATE, L"%u KB", me.modBaseSize / 1024);
			g_moduleList.SetItemText(item, 2, sizeStr);
		}
		while (Module32NextW(hSnap, &me));
	}
	CloseHandle(hSnap);
	g_moduleList.SetRedraw(true);
}

static void ShowInExplorer(const WCHAR* path)
{
	if (!path || !path[0]) return;
	WCHAR cmd[MAX_PATH + 32];
	_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"/select,\"%s\"", path);
	ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd, nullptr, SW_SHOWNORMAL);
}

static void KillSelectedProcess()
{
	const int sel = g_procList.GetSelected();
	if (sel < 0) return;
	const DWORD pid = static_cast<DWORD>(g_procList.GetItemData(sel));
	if (MessageBox(g_hMainWnd, L"Terminate this process?", L"SysMate", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
		return;
	const HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (hProc)
	{
		TerminateProcess(hProc, 1);
		CloseHandle(hProc);
		RefreshProcessList();
	}
}

static void OnProcessContextMenu(const HWND hWnd, const int x, const int y)
{
	const int sel = g_procList.GetSelected();
	if (sel < 0) return;
	const HMENU hMenu = CreatePopupMenu();
	AppendMenu(hMenu, MF_STRING, 1, L"Show in Explorer");
	AppendMenu(hMenu, MF_STRING, 2, L"Kill Process");
	const int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hWnd, nullptr);
	DestroyMenu(hMenu);
	if (cmd == 1 && sel < static_cast<int>(g_processes.size()) && !g_processes[sel].path.empty())
		ShowInExplorer(g_processes[sel].path.c_str());
	else if (cmd == 2)
		KillSelectedProcess();
}

static void OnModuleContextMenu(const HWND hWnd, const int x, const int y)
{
	const int sel = g_moduleList.GetSelected();
	if (sel < 0) return;
	const std::wstring path = g_moduleList.GetItemText(sel, 1);
	const HMENU hMenu = CreatePopupMenu();
	AppendMenu(hMenu, MF_STRING, 1, L"Show in Explorer");
	const int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hWnd, nullptr);
	DestroyMenu(hMenu);
	if (cmd == 1 && !path.empty()) ShowInExplorer(path.c_str());
}

// ============================================================
// Network tab
// ============================================================
static CustomListView g_netList;

static const WCHAR* TcpStateStr(const DWORD state)
{
	switch (state)
	{
	case MIB_TCP_STATE_CLOSED: return L"CLOSED";
	case MIB_TCP_STATE_LISTEN: return L"LISTENING";
	case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
	case MIB_TCP_STATE_SYN_RCVD: return L"SYN_RCVD";
	case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
	case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
	case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
	case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
	case MIB_TCP_STATE_CLOSING: return L"CLOSING";
	case MIB_TCP_STATE_LAST_ACK: return L"LAST_ACK";
	case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
	default: return L"?";
	}
}

static void FormatAddr(DWORD ip, const DWORD port, WCHAR* buf, const int bufLen)
{
	const WORD p = ntohs(static_cast<WORD>(port));
	const auto b = reinterpret_cast<BYTE*>(&ip);
	_snwprintf_s(buf, bufLen, _TRUNCATE, L"%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], p);
}

static void RefreshNetworkList()
{
	g_netList.SetRedraw(false);
	g_netList.DeleteAllItems();

	ULONG size = 0;
	GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
	if (size)
	{
		auto* table = static_cast<MIB_TCPTABLE_OWNER_PID*>(HeapAlloc(GetProcessHeap(), 0, size));
		if (table && GetExtendedTcpTable(table, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
		{
			for (DWORD i = 0; i < table->dwNumEntries; i++)
			{
				const auto& row = table->table[i];
				WCHAR local[64], remote[64], pid[16];
				FormatAddr(row.dwLocalAddr, row.dwLocalPort, local, _countof(local));
				FormatAddr(row.dwRemoteAddr, row.dwRemotePort, remote, _countof(remote));
				_snwprintf_s(pid, _countof(pid), _TRUNCATE, L"%u", row.dwOwningPid);
				const int idx = g_netList.AddItem(L"TCP");
				g_netList.SetItemText(idx, 1, local);
				g_netList.SetItemText(idx, 2, remote);
				g_netList.SetItemText(idx, 3, TcpStateStr(row.dwState));
				g_netList.SetItemText(idx, 4, pid);
			}
		}
		if (table) HeapFree(GetProcessHeap(), 0, table);
	}

	size = 0;
	GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
	if (size)
	{
		auto* table = static_cast<MIB_UDPTABLE_OWNER_PID*>(HeapAlloc(GetProcessHeap(), 0, size));
		if (table && GetExtendedUdpTable(table, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
		{
			for (DWORD i = 0; i < table->dwNumEntries; i++)
			{
				const auto& row = table->table[i];
				WCHAR local[64], pid[16];
				FormatAddr(row.dwLocalAddr, row.dwLocalPort, local, _countof(local));
				_snwprintf_s(pid, _countof(pid), _TRUNCATE, L"%u", row.dwOwningPid);
				const int idx = g_netList.AddItem(L"UDP");
				g_netList.SetItemText(idx, 1, local);
				g_netList.SetItemText(idx, 4, pid);
			}
		}
		if (table) HeapFree(GetProcessHeap(), 0, table);
	}

	g_netList.SetRedraw(true);
}

// ============================================================
// Windows tab
// ============================================================
static CustomListView g_winList;
static CustomListView g_winPropList;

struct WindowInfo
{
	HWND hwnd;
	std::wstring title;
	std::wstring className;
	DWORD pid;
	DWORD tid;
	RECT rect;
};

static std::vector<WindowInfo> g_windows;

static BOOL CALLBACK EnumWindowsForList(const HWND hwnd, const LPARAM lParam)
{
	if (!IsWindowVisible(hwnd)) return TRUE;
	WindowInfo wi;
	wi.hwnd = hwnd;
	WCHAR buf[256] = {};
	GetWindowTextW(hwnd, buf, _countof(buf));
	wi.title = buf;
	GetClassNameW(hwnd, buf, _countof(buf));
	wi.className = buf;
	wi.tid = GetWindowThreadProcessId(hwnd, &wi.pid);
	GetWindowRect(hwnd, &wi.rect);

	if (!MatchesFilter(wi.title + L" " + wi.className)) return TRUE;

	auto* vec = (std::vector<WindowInfo>*)lParam;
	vec->push_back(wi);
	return TRUE;
}

static void RefreshWindowList()
{
	g_winList.SetRedraw(false);
	g_winList.DeleteAllItems();
	g_windows.clear();
	EnumWindows(EnumWindowsForList, (LPARAM)&g_windows);

	for (int i = 0; i < static_cast<int>(g_windows.size()); i++)
	{
		auto& wi = g_windows[i];
		const int idx = g_winList.AddItem(wi.title.empty() ? wi.className.c_str() : wi.title.c_str());
		g_winList.SetItemText(idx, 1, wi.className.c_str());
		WCHAR pidStr[16];
		_snwprintf_s(pidStr, _countof(pidStr), _TRUNCATE, L"%u", wi.pid);
		g_winList.SetItemText(idx, 2, pidStr);
		WCHAR handle[20];
		_snwprintf_s(handle, _countof(handle), _TRUNCATE, L"%08X", static_cast<unsigned>((UINT_PTR)wi.hwnd));
		g_winList.SetItemText(idx, 3, handle);
	}

	g_winList.SetRedraw(true);
}

static void ShowWindowProperties(const int selIdx)
{
	g_winPropList.SetRedraw(false);
	g_winPropList.DeleteAllItems();
	if (selIdx < 0 || selIdx >= static_cast<int>(g_windows.size()))
	{
		g_winPropList.SetRedraw(true);
		return;
	}
	auto& wi = g_windows[selIdx];

	auto addProp = [](const WCHAR* name, const WCHAR* value)
	{
		const int item = g_winPropList.AddItem(name);
		g_winPropList.SetItemText(item, 1, value);
	};

	addProp(L"Title", wi.title.c_str());
	addProp(L"Class", wi.className.c_str());
	WCHAR buf[128];
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", wi.pid);
	addProp(L"PID", buf);
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", wi.tid);
	addProp(L"Thread ID", buf);
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%08X", static_cast<unsigned>((UINT_PTR)wi.hwnd));
	addProp(L"Handle", buf);
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d, %d, %d, %d", wi.rect.left, wi.rect.top, wi.rect.right,
	             wi.rect.bottom);
	addProp(L"Rect", buf);
	const LONG style = GetWindowLong(wi.hwnd, GWL_STYLE);
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"0x%08X", static_cast<unsigned>(style));
	addProp(L"Style", buf);
	const LONG exStyle = GetWindowLong(wi.hwnd, GWL_EXSTYLE);
	_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"0x%08X", static_cast<unsigned>(exStyle));
	addProp(L"ExStyle", buf);

	g_winPropList.SetRedraw(true);
}

// ============================================================
// About tab (custom drawn panel)
// ============================================================

static void RefreshAboutInfo()
{
	if (g_hAboutPanel) InvalidateRect(g_hAboutPanel, nullptr, FALSE);
}

static void DrawDarkButton(const DRAWITEMSTRUCT* dis)
{
	const HDC hdc = dis->hDC;
	const RECT rc = dis->rcItem;
	const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
	const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
	const bool focused = (dis->itemState & ODS_FOCUS) != 0;

	COLORREF bg = pressed ? RGB(0, 100, 180) : RGB(55, 55, 60);
	if (disabled) bg = RGB(40, 40, 43);
	const HBRUSH br = CreateSolidBrush(bg);
	FillRect(hdc, &rc, br);
	DeleteObject(br);

	const HPEN pen = CreatePen(PS_SOLID, 1, focused && !disabled ? RGB(0, 150, 255) : Dark::Border);
	const auto oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
	SelectObject(hdc, GetStockObject(NULL_BRUSH));
	Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
	SelectObject(hdc, oldPen);
	DeleteObject(pen);

	WCHAR text[64] = {};
	GetWindowTextW(dis->hwndItem, text, 64);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, disabled ? RGB(100, 100, 100) : Dark::Text);
	SelectObject(hdc, g_hFont);
	DrawTextExt(hdc, text, -1, rc, TextAlign::Center | TextAlign::VCenter);
}

static void UpdateProcessButtons()
{
	const int sel = g_procList.GetSelected();
	const bool hasSel = sel >= 0 && sel < static_cast<int>(g_processes.size());
	EnableWindow(g_btnExplorerProc, hasSel && !g_processes[sel].path.empty());
	EnableWindow(g_btnKillProc, hasSel);
}

static void UpdateWindowButtons()
{
	const int sel = g_winList.GetSelected();
	const bool hasSel = sel >= 0 && sel < static_cast<int>(g_windows.size());
	EnableWindow(g_btnExplorerWin, hasSel);
	EnableWindow(g_btnKillWin, hasSel);
}

static void ShowWindowProcessInExplorer()
{
	const int sel = g_winList.GetSelected();
	if (sel < 0 || sel >= static_cast<int>(g_windows.size())) return;
	const HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, g_windows[sel].pid);
	if (hProc)
	{
		WCHAR path[MAX_PATH] = {};
		DWORD len = MAX_PATH;
		if (QueryFullProcessImageNameW(hProc, 0, path, &len))
			ShowInExplorer(path);
		CloseHandle(hProc);
	}
}

static void KillSelectedWindow()
{
	const int sel = g_winList.GetSelected();
	if (sel < 0 || sel >= static_cast<int>(g_windows.size())) return;
	if (MessageBox(g_hMainWnd, L"Close this window?", L"SysMate", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
		return;
	PostMessage(g_windows[sel].hwnd, WM_CLOSE, 0, 0);
	RefreshWindowList();
}

static LRESULT CALLBACK AboutPanelProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	switch (msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			const HDC hdc = BeginPaint(hWnd, &ps);
			RECT rc;
			GetClientRect(hWnd, &rc);

			const HDC memDC = CreateCompatibleDC(hdc);
			const HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
			const auto oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

			FillRect(memDC, &rc, Dark::BrushWindow());
			SetBkMode(memDC, TRANSPARENT);

			const int leftMargin = Dpi::Scale(Layout::AboutLeftMargin);
			const int rightEdge = rc.right - Dpi::Scale(Layout::AboutRightMargin);
			int y = Dpi::Scale(Layout::AboutTopMargin);

			// Title
			SelectObject(memDC, g_hFontTitle);
			SetTextColor(memDC, RGB(255, 255, 255));
			const RECT titleRc = {leftMargin, y, rightEdge, y + Dpi::Scale(Layout::AboutTitleHeight)};
			DrawTextExt(memDC, L"SysMate", -1, titleRc, TextAlign::Left);
			y += Dpi::Scale(Layout::AboutTitleSpacing);

			// Subtitle
			SelectObject(memDC, g_hFontSubtitle);
			SetTextColor(memDC, RGB(180, 180, 180));
			const RECT verRc = {leftMargin, y, rightEdge, y + Dpi::Scale(Layout::AboutSubtitleHeight)};
			DrawTextExt(memDC, L"Version 2.0 \u2014 System Information Utility", -1, verRc, TextAlign::Left);
			y += Dpi::Scale(Layout::AboutSubtitleSpacing);

			// Copyright
			SelectObject(memDC, g_hFont);
			SetTextColor(memDC, RGB(140, 140, 140));
			const RECT copyrRc = {leftMargin, y, rightEdge, y + Dpi::Scale(Layout::AboutCopyrightHeight)};
			DrawTextExt(memDC, L"Copyright \u00A9 2002 Zac Walker", -1, copyrRc, TextAlign::Left);
			y += Dpi::Scale(Layout::AboutPostCopyrightSpacing);

			// Separator
			const HPEN pen = CreatePen(PS_SOLID, 1, Dark::Border);
			const auto oldPen = static_cast<HPEN>(SelectObject(memDC, pen));
			MoveToEx(memDC, leftMargin, y, nullptr);
			LineTo(memDC, rightEdge, y);
			SelectObject(memDC, oldPen);
			DeleteObject(pen);
			y += Dpi::Scale(Layout::AboutPostSepSpacing);

			// System info rows
			auto drawRow = [&](const WCHAR* label, const WCHAR* value)
			{
				const int rowH = Dpi::Scale(Layout::AboutRowHeight);
				const int labelW = Dpi::Scale(Layout::AboutLabelWidth);
				const int valueX = leftMargin + Dpi::Scale(Layout::AboutValueOffset);
				SelectObject(memDC, g_hFontBold);
				SetTextColor(memDC, RGB(140, 140, 140));
				const RECT lr = {leftMargin, y, leftMargin + labelW, y + rowH};
				DrawTextExt(memDC, label, -1, lr, TextAlign::Left | TextAlign::VCenter);
				SelectObject(memDC, g_hFont);
				SetTextColor(memDC, RGB(220, 220, 220));
				const RECT vr = {valueX, y, rightEdge, y + rowH};
				DrawTextExt(memDC, value, -1, vr, TextAlign::Left | TextAlign::VCenter);
				y += Dpi::Scale(Layout::AboutRowSpacing);
			};

			WCHAR buf[256];
			DWORD nameLen = _countof(buf);
			GetComputerNameW(buf, &nameLen);
			drawRow(L"Computer Name", buf);
			nameLen = _countof(buf);
			GetUserNameW(buf, &nameLen);
			drawRow(L"User Name", buf);

			OSVERSIONINFOEXW ovi = {sizeof(ovi)};
#pragma warning(suppress: 4996)
			GetVersionExW((OSVERSIONINFOW*)&ovi);
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u.%u.%u", ovi.dwMajorVersion, ovi.dwMinorVersion,
			             ovi.dwBuildNumber);
			drawRow(L"OS Version", buf);

			SYSTEM_INFO si = {};
			GetNativeSystemInfo(&si);
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u", si.dwNumberOfProcessors);
			drawRow(L"Processors", buf);

			switch (si.wProcessorArchitecture)
			{
			case PROCESSOR_ARCHITECTURE_AMD64: drawRow(L"Architecture", L"x64");
				break;
			case PROCESSOR_ARCHITECTURE_ARM64: drawRow(L"Architecture", L"ARM64");
				break;
			case PROCESSOR_ARCHITECTURE_INTEL: drawRow(L"Architecture", L"x86");
				break;
			default: drawRow(L"Architecture", L"Unknown");
				break;
			}

			MEMORYSTATUSEX ms = {sizeof(ms)};
			GlobalMemoryStatusEx(&ms);
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f GB", ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
			drawRow(L"Total RAM", buf);
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f GB", ms.ullAvailPhys / (1024.0 * 1024.0 * 1024.0));
			drawRow(L"Available RAM", buf);
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u%%", ms.dwMemoryLoad);
			drawRow(L"Memory Load", buf);

			const ULONGLONG uptime = GetTickCount64();
			const ULONGLONG hours = uptime / 3600000;
			const ULONGLONG mins = (uptime % 3600000) / 60000;
			_snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%llu hours, %llu minutes", hours, mins);
			drawRow(L"System Uptime", buf);

			BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
			SelectObject(memDC, oldBmp);
			DeleteObject(memBmp);
			DeleteDC(memDC);

			EndPaint(hWnd, &ps);
			return 0;
		}
	case WM_ERASEBKGND: return 1;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// Tab page proc
// ============================================================
static LRESULT CALLBACK TabPageProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	switch (msg)
	{
	case WM_ERASEBKGND:
		{
			const auto hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, Dark::BrushWindow());
			return 1;
		}
	case WM_CTLCOLOREDIT:
		return Dark::OnCtlColorEdit((HDC)wParam);
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// Layout
// ============================================================
static constexpr int SPLITTER_WIDTH = 4;
static int g_procSplitter = 0;
static int g_winSplitter = 0;

static void LayoutTabPages()
{
	RECT rc;
	GetClientRect(g_hMainWnd, &rc);
	const int tabH = Dpi::Scale(Layout::TabHeight);
	const bool hasBottomBar = (g_nCurrentTab != TAB_PERFORMANCE);
	const int barH = hasBottomBar ? Dpi::Scale(Layout::BottomBarHeight) : 0;

	const int pageH = rc.bottom - tabH - barH;
	const int pageW = rc.right;

	for (int i = 0; i < TAB_COUNT; i++)
		if (g_hTabPages[i])
			MoveWindow(g_hTabPages[i], 0, tabH, pageW, pageH, TRUE);

	// Bottom bar: filter (left) + buttons (right), all on one line
	const int barY = rc.bottom - barH;
	const int btnH = Dpi::Scale(Layout::ButtonHeight);
	const int btnW = Dpi::Scale(Layout::ButtonWidth);
	const int btnSpace = Dpi::Scale(Layout::ButtonSpacing);
	const int btnY = barY + (barH - btnH) / 2;

	// Filter edit
	const bool needsFilter = (g_nCurrentTab == TAB_PROCESSES || g_nCurrentTab == TAB_WINDOWS);
	ShowWindow(g_hFilterEdit, (hasBottomBar && needsFilter) ? SW_SHOW : SW_HIDE);
	if (hasBottomBar && needsFilter)
	{
		const int filterW = Dpi::Scale(Layout::FilterWidth);
		MoveWindow(g_hFilterEdit, btnSpace, btnY, filterW, btnH, TRUE);
	}

	// Hide all buttons then show per-tab
	auto hideBtn = [](const HWND btn) { if (btn) ShowWindow(btn, SW_HIDE); };
	hideBtn(g_btnRefreshProc);
	hideBtn(g_btnExplorerProc);
	hideBtn(g_btnKillProc);
	hideBtn(g_btnRefreshNet);
	hideBtn(g_btnRefreshWin);
	hideBtn(g_btnExplorerWin);
	hideBtn(g_btnKillWin);
	hideBtn(g_btnRefreshAbout);

	auto layoutButtons = [&](const std::vector<HWND>& btns)
	{
		int btnX = rc.right;
		for (int i = static_cast<int>(btns.size()) - 1; i >= 0; i--)
		{
			btnX -= btnSpace + btnW;
			MoveWindow(btns[i], btnX, btnY, btnW, btnH, TRUE);
			ShowWindow(btns[i], SW_SHOW);
		}
	};

	switch (g_nCurrentTab)
	{
	case TAB_PROCESSES:
		layoutButtons({g_btnRefreshProc, g_btnExplorerProc, g_btnKillProc});
		break;
	case TAB_NETWORK:
		layoutButtons({g_btnRefreshNet});
		break;
	case TAB_WINDOWS:
		layoutButtons({g_btnRefreshWin, g_btnExplorerWin, g_btnKillWin});
		break;
	case TAB_ABOUT:
		layoutButtons({g_btnRefreshAbout});
		break;
	}

	// Performance
	if (g_hPerfPanel)
	{
		RECT pr;
		GetClientRect(g_hTabPages[TAB_PERFORMANCE], &pr);
		MoveWindow(g_hPerfPanel, 0, 0, pr.right, pr.bottom, TRUE);
	}

	// Process split (always 50%)
	if (g_procList.GetHWND() && g_moduleList.GetHWND())
	{
		RECT pr;
		GetClientRect(g_hTabPages[TAB_PROCESSES], &pr);
		const int splitter = Dpi::Scale(SPLITTER_WIDTH);
		g_procSplitter = pr.right / 2;
		MoveWindow(g_procList.GetHWND(), 0, 0, g_procSplitter, pr.bottom, TRUE);
		MoveWindow(g_moduleList.GetHWND(), g_procSplitter + splitter, 0,
		           pr.right - g_procSplitter - splitter, pr.bottom, TRUE);
	}

	// Network
	if (g_netList.GetHWND())
	{
		RECT pr;
		GetClientRect(g_hTabPages[TAB_NETWORK], &pr);
		MoveWindow(g_netList.GetHWND(), 0, 0, pr.right, pr.bottom, TRUE);
	}

	// Windows split (always 50%)
	if (g_winList.GetHWND() && g_winPropList.GetHWND())
	{
		RECT pr;
		GetClientRect(g_hTabPages[TAB_WINDOWS], &pr);
		const int splitter = Dpi::Scale(SPLITTER_WIDTH);
		g_winSplitter = pr.right / 2;
		MoveWindow(g_winList.GetHWND(), 0, 0, g_winSplitter, pr.bottom, TRUE);
		MoveWindow(g_winPropList.GetHWND(), g_winSplitter + splitter, 0,
		           pr.right - g_winSplitter - splitter, pr.bottom, TRUE);
	}

	// About
	if (g_hAboutPanel)
	{
		RECT pr;
		GetClientRect(g_hTabPages[TAB_ABOUT], &pr);
		MoveWindow(g_hAboutPanel, 0, 0, pr.right, pr.bottom, TRUE);
	}
}

static void SwitchTab(const int newTab)
{
	if (newTab < 0 || newTab >= TAB_COUNT) return;

	// Save current tab's filter text
	WCHAR buf[256] = {};
	GetWindowTextW(g_hFilterEdit, buf, _countof(buf));
	g_tabFilterText[g_nCurrentTab] = buf;

	if (g_hTabPages[g_nCurrentTab]) ShowWindow(g_hTabPages[g_nCurrentTab], SW_HIDE);
	g_nCurrentTab = newTab;
	if (g_hTabPages[g_nCurrentTab]) ShowWindow(g_hTabPages[g_nCurrentTab], SW_SHOW);
	g_tabCtrl.SetCurSel(g_nCurrentTab);

	// Restore new tab's filter text
	g_filterText = g_tabFilterText[g_nCurrentTab];
	SetWindowTextW(g_hFilterEdit, g_filterText.c_str());

	LayoutTabPages();
}

static void RefreshCurrentView()
{
	switch (g_nCurrentTab)
	{
	case TAB_PROCESSES: RefreshProcessList();
		break;
	case TAB_NETWORK: RefreshNetworkList();
		break;
	case TAB_WINDOWS: RefreshWindowList();
		break;
	case TAB_ABOUT: RefreshAboutInfo();
		break;
	case TAB_PERFORMANCE: InvalidateRect(g_hPerfPanel, nullptr, FALSE);
		break;
	}
}

// ============================================================
// Main window
// ============================================================
static LRESULT CALLBACK MainWndProc(const HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		{
			// Initialize DPI scaling
			Dpi::g_scale = GetDpiForWindow(hWnd) / 96.0f;

			g_hFont = CreateFont(Dpi::Scale(-13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			                     0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
			g_hFontBold = CreateFont(Dpi::Scale(-13), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			                         0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
			g_hFontChart = CreateFont(Dpi::Scale(-20), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			                          0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
			g_hFontTitle = CreateFont(Dpi::Scale(-32), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			                          0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
			g_hFontSubtitle = CreateFont(Dpi::Scale(-18), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			                             0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

			// Custom tab control
			g_tabCtrl.Create(hWnd, g_hInst, Dpi::Scale(Layout::TabHeight), g_hFont);
			for (int i = 0; i < TAB_COUNT; i++)
				g_tabCtrl.AddTab(g_tabNames[i]);
			g_tabCtrl.SetChangeCallback([](const int idx) { SwitchTab(idx); });

			WNDCLASS wcp = {};
			wcp.lpfnWndProc = TabPageProc;
			wcp.hInstance = g_hInst;
			wcp.lpszClassName = L"SysMateTabPage";
			RegisterClass(&wcp);

			for (int i = 0; i < TAB_COUNT; i++)
				g_hTabPages[i] = CreateWindowEx(0, L"SysMateTabPage", L"",
				                                WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100, hWnd, nullptr, g_hInst,
				                                nullptr);

			WNDCLASS wperf = {};
			wperf.lpfnWndProc = PerfPanelProc;
			wperf.hInstance = g_hInst;
			wperf.lpszClassName = L"SysMatePerfPanel";
			RegisterClass(&wperf);

			g_hPerfPanel = CreateWindowEx(0, L"SysMatePerfPanel", L"",
			                              WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, g_hTabPages[TAB_PERFORMANCE], nullptr,
			                              g_hInst, nullptr);
			InitPerfCounters();
			g_perfTimer = SetTimer(g_hPerfPanel, 1, 1000, nullptr);

			// Process list
			g_procList.Create(g_hTabPages[TAB_PROCESSES], g_hInst, g_hFont, g_hFontBold);
			g_procList.AddColumn(L"Name", 200);
			g_procList.AddColumn(L"PID", 70);
			g_procList.AddColumn(L"Memory", 100);
			g_procList.SetSelectionCallback([](const int idx)
			{
				const DWORD pid = static_cast<DWORD>(g_procList.GetItemData(idx));
				RefreshModuleList(pid);
				UpdateProcessButtons();
			});
			g_procList.SetContextMenuCallback([](const HWND hw, const int x, const int y)
			{
				OnProcessContextMenu(GetParent(GetParent(hw)), x, y);
			});

			// Module list
			g_moduleList.Create(g_hTabPages[TAB_PROCESSES], g_hInst, g_hFont, g_hFontBold);
			g_moduleList.AddColumn(L"Module", 180);
			g_moduleList.AddColumn(L"Path", 300);
			g_moduleList.AddColumn(L"Size", 80);
			g_moduleList.SetContextMenuCallback([](const HWND hw, const int x, const int y)
			{
				OnModuleContextMenu(GetParent(GetParent(hw)), x, y);
			});

			// Network list
			g_netList.Create(g_hTabPages[TAB_NETWORK], g_hInst, g_hFont, g_hFontBold);
			g_netList.AddColumn(L"Proto", 60);
			g_netList.AddColumn(L"Local Address", 180);
			g_netList.AddColumn(L"Remote Address", 180);
			g_netList.AddColumn(L"State", 110);
			g_netList.AddColumn(L"PID", 70);

			// Windows list
			g_winList.Create(g_hTabPages[TAB_WINDOWS], g_hInst, g_hFont, g_hFontBold);
			g_winList.AddColumn(L"Title", 250);
			g_winList.AddColumn(L"Class", 150);
			g_winList.AddColumn(L"PID", 70);
			g_winList.AddColumn(L"Handle", 80);
			g_winList.SetSelectionCallback([](const int idx)
			{
				ShowWindowProperties(idx);
				UpdateWindowButtons();
			});

			// Window properties list
			g_winPropList.Create(g_hTabPages[TAB_WINDOWS], g_hInst, g_hFont, g_hFontBold);
			g_winPropList.AddColumn(L"Property", 120);
			g_winPropList.AddColumn(L"Value", 300);

			// About panel
			WNDCLASS wabout = {};
			wabout.lpfnWndProc = AboutPanelProc;
			wabout.hInstance = g_hInst;
			wabout.lpszClassName = L"SysMateAboutPanel";
			RegisterClass(&wabout);
			g_hAboutPanel = CreateWindowEx(0, L"SysMateAboutPanel", L"",
			                               WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
			                               g_hTabPages[TAB_ABOUT], nullptr, g_hInst, nullptr);

			// Create buttons (owner-draw, parented to main window for bottom bar)
			const int btnW = Dpi::Scale(Layout::ButtonWidth);
			const int btnH = Dpi::Scale(Layout::ButtonHeight);
			auto makeBtn = [&](const int id, const WCHAR* text) -> HWND
			{
				const HWND btn = CreateWindowEx(0, L"BUTTON", text,
				                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
				                                0, 0, btnW, btnH, hWnd,
				                                (HMENU)static_cast<INT_PTR>(id), g_hInst, nullptr);
				SendMessage(btn, WM_SETFONT, (WPARAM)g_hFont, 0);
				return btn;
			};

			g_btnRefreshProc = makeBtn(BTN_REFRESH_PROC, L"Refresh");
			g_btnExplorerProc = makeBtn(BTN_EXPLORER_PROC, L"Open in Explorer");
			g_btnKillProc = makeBtn(BTN_KILL_PROC, L"Kill Process");
			EnableWindow(g_btnExplorerProc, FALSE);
			EnableWindow(g_btnKillProc, FALSE);

			g_btnRefreshNet = makeBtn(BTN_REFRESH_NET, L"Refresh");

			g_btnRefreshWin = makeBtn(BTN_REFRESH_WIN, L"Refresh");
			g_btnExplorerWin = makeBtn(BTN_EXPLORER_WIN, L"Open in Explorer");
			g_btnKillWin = makeBtn(BTN_KILL_WIN, L"Close Window");
			EnableWindow(g_btnExplorerWin, FALSE);
			EnableWindow(g_btnKillWin, FALSE);

			g_btnRefreshAbout = makeBtn(BTN_REFRESH_ABOUT, L"Refresh");

			g_hFilterEdit = CreateWindowEx(0, L"EDIT", L"",
			                               WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
			                               0, 0, Dpi::Scale(Layout::FilterWidth), btnH,
			                               hWnd, (HMENU)9999, g_hInst, nullptr);
			SendMessage(g_hFilterEdit, WM_SETFONT, (WPARAM)g_hFont, 0);
			SendMessage(g_hFilterEdit, EM_SETCUEBANNER, 0, (LPARAM)L"Filter...");
			SetWindowTheme(g_hFilterEdit, L"", L"");

			WCHAR compName[MAX_COMPUTERNAME_LENGTH + 32];
			DWORD nameLen2 = MAX_COMPUTERNAME_LENGTH + 1;
			if (GetComputerNameW(compName, &nameLen2))
			{
				WCHAR title[256];
				_snwprintf_s(title, _countof(title), _TRUNCATE, L"SysMate - %s", compName);
				SetWindowText(hWnd, title);
			}

			g_nCurrentTab = TAB_PERFORMANCE;
			for (int i = 0; i < TAB_COUNT; i++)
				ShowWindow(g_hTabPages[i], i == g_nCurrentTab ? SW_SHOW : SW_HIDE);

			RefreshProcessList();
			RefreshNetworkList();
			RefreshWindowList();
			RefreshAboutInfo();
			return 0;
		}

	case WM_SIZE:
		{
			RECT rc;
			GetClientRect(hWnd, &rc);
			MoveWindow(g_tabCtrl.GetHWND(), 0, 0, rc.right, Dpi::Scale(Layout::TabHeight), TRUE);
			LayoutTabPages();
			return 0;
		}

	case WM_CTLCOLOREDIT:
		return Dark::OnCtlColorEdit((HDC)wParam);

	case WM_DRAWITEM:
		{
			const auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
			if (dis->CtlType == ODT_BUTTON)
			{
				DrawDarkButton(dis);
				return TRUE;
			}
			break;
		}

	case WM_ERASEBKGND:
		{
			const auto hdc = (HDC)wParam;
			RECT rc;
			GetClientRect(hWnd, &rc);
			FillRect(hdc, &rc, Dark::BrushWindow());
			return 1;
		}

	case WM_COMMAND:
		if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == 9999)
		{
			WCHAR buf[256] = {};
			GetWindowTextW(g_hFilterEdit, buf, _countof(buf));
			g_filterText = buf;
			g_tabFilterText[g_nCurrentTab] = g_filterText;
			if (g_nCurrentTab == TAB_PROCESSES) RefreshProcessList();
			else if (g_nCurrentTab == TAB_WINDOWS) RefreshWindowList();
			return 0;
		}
		switch (LOWORD(wParam))
		{
		case BTN_REFRESH_PROC: RefreshProcessList();
			return 0;
		case BTN_EXPLORER_PROC:
			{
				const int sel = g_procList.GetSelected();
				if (sel >= 0 && sel < static_cast<int>(g_processes.size()) && !g_processes[sel].path.empty())
					ShowInExplorer(g_processes[sel].path.c_str());
				return 0;
			}
		case BTN_KILL_PROC: KillSelectedProcess();
			return 0;
		case BTN_REFRESH_NET: RefreshNetworkList();
			return 0;
		case BTN_REFRESH_WIN: RefreshWindowList();
			return 0;
		case BTN_EXPLORER_WIN: ShowWindowProcessInExplorer();
			return 0;
		case BTN_KILL_WIN: KillSelectedWindow();
			return 0;
		case BTN_REFRESH_ABOUT: RefreshAboutInfo();
			return 0;
		}
		break;

	case WM_KEYDOWN:
		if (wParam == VK_F5)
		{
			RefreshCurrentView();
			return 0;
		}
		break;

	case WM_DESTROY:
		if (g_perfTimer) KillTimer(g_hPerfPanel, g_perfTimer);
		if (g_pdhQuery) PdhCloseQuery(g_pdhQuery);
		if (g_hFont) DeleteObject(g_hFont);
		if (g_hFontBold) DeleteObject(g_hFontBold);
		if (g_hFontChart) DeleteObject(g_hFontChart);
		if (g_hFontTitle) DeleteObject(g_hFontTitle);
		if (g_hFontSubtitle) DeleteObject(g_hFontSubtitle);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(const HINSTANCE hInstance, HINSTANCE, LPWSTR, const int nCmdShow)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	g_hInst = hInstance;
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	WNDCLASSEX wc = {sizeof(wc)};
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SYSMATE));
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = L"SysMateMainClass";
	wc.hIconSm = wc.hIcon;
	RegisterClassEx(&wc);

	g_hMainWnd = CreateWindowEx(0, L"SysMateMainClass", L"SysMate",
	                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
	                            CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650,
	                            nullptr, nullptr, hInstance, nullptr);

	ShowWindow(g_hMainWnd, nCmdShow);
	UpdateWindow(g_hMainWnd);

	const HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCEL));
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(g_hMainWnd, hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	CoUninitialize();
	return static_cast<int>(msg.wParam);
}
