#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include "dark_theme.h"
#include "layout.h"

// ============================================================
// Custom Tab Control
// ============================================================
class CustomTabControl
{
public:
	struct Tab { std::wstring text; };

	void Create(HWND hParent, HINSTANCE hInst, int height, HFONT hFont)
	{
		m_height = height;
		m_hFont = hFont;

		WNDCLASSW wc = {};
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInst;
		wc.lpszClassName = L"SysMateCustomTab";
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		RegisterClassW(&wc);

		m_hWnd = CreateWindowExW(0, L"SysMateCustomTab", L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
			0, 0, 100, height, hParent, nullptr, hInst, this);
	}

	void AddTab(const WCHAR* text) { m_tabs.push_back({text}); InvalidateRect(m_hWnd, nullptr, FALSE); }
	int GetCurSel() const { return m_selected; }

	void SetCurSel(int idx)
	{
		if (idx >= 0 && idx < (int)m_tabs.size())
		{
			m_selected = idx;
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	HWND GetHWND() const { return m_hWnd; }
	void SetChangeCallback(std::function<void(int)> cb) { m_onChange = cb; }

private:
	HWND m_hWnd = nullptr;
	HFONT m_hFont = nullptr;
	int m_height = 30;
	int m_selected = 0;
	int m_hot = -1;
	std::vector<Tab> m_tabs;
	std::function<void(int)> m_onChange;

	int HitTest(int x, int y) const
	{
		if (m_tabs.empty()) return -1;
		RECT rc;
		GetClientRect(m_hWnd, &rc);
		int tabW = rc.right / (int)m_tabs.size();
		if (tabW < 60) tabW = 60;
		for (int i = 0; i < (int)m_tabs.size(); i++)
		{
			RECT tr = {i * tabW, 0, (i + 1) * tabW, rc.bottom};
			POINT pt = {x, y};
			if (PtInRect(&tr, pt)) return i;
		}
		return -1;
	}

	void OnPaint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(m_hWnd, &ps);
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		// Double-buffer
		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
		HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

		FillRect(memDC, &rc, Dark::BrushTab());

		if (!m_tabs.empty())
		{
			int tabW = rc.right / (int)m_tabs.size();
			if (tabW < 60) tabW = 60;

			SelectObject(memDC, m_hFont);
			SetBkMode(memDC, TRANSPARENT);

			for (int i = 0; i < (int)m_tabs.size(); i++)
			{
				RECT tr = {i * tabW, 0, (i + 1) * tabW, rc.bottom};
				bool sel = (i == m_selected);
				bool hot = (i == m_hot && !sel);

				if (sel)
					FillRect(memDC, &tr, Dark::BrushTabSel());
				else if (hot)
				{
					HBRUSH hbr = CreateSolidBrush(Dark::BgHot);
					FillRect(memDC, &tr, hbr);
					DeleteObject(hbr);
				}

				if (sel)
				{
					RECT accent = tr;
					accent.bottom = accent.top + 2;
					HBRUSH acBr = CreateSolidBrush(RGB(0, 150, 255));
					FillRect(memDC, &accent, acBr);
					DeleteObject(acBr);
				}

				SetTextColor(memDC, sel ? Dark::Text : Dark::TextDim);
				DrawTextExt(memDC, m_tabs[i].text.c_str(), -1, tr, TextAlign::Center | TextAlign::VCenter);
			}
		}

		// Bottom border
		HPEN pen = CreatePen(PS_SOLID, 1, Dark::Border);
		HPEN oldPen = (HPEN)SelectObject(memDC, pen);
		MoveToEx(memDC, 0, rc.bottom - 1, nullptr);
		LineTo(memDC, rc.right, rc.bottom - 1);
		SelectObject(memDC, oldPen);
		DeleteObject(pen);

		BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
		SelectObject(memDC, oldBmp);
		DeleteObject(memBmp);
		DeleteDC(memDC);

		EndPaint(m_hWnd, &ps);
	}

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		CustomTabControl* self = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = (CREATESTRUCT*)lParam;
			self = (CustomTabControl*)cs->lpCreateParams;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
		}
		else
		{
			self = (CustomTabControl*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		}
		if (!self) return DefWindowProc(hWnd, msg, wParam, lParam);

		switch (msg)
		{
		case WM_PAINT: self->OnPaint(); return 0;
		case WM_ERASEBKGND: return 1;
		case WM_LBUTTONDOWN:
		{
			int idx = self->HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			if (idx >= 0 && idx != self->m_selected)
			{
				self->m_selected = idx;
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onChange) self->m_onChange(idx);
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			int idx = self->HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			if (idx != self->m_hot)
			{
				self->m_hot = idx;
				InvalidateRect(hWnd, nullptr, FALSE);
				TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
				TrackMouseEvent(&tme);
			}
			return 0;
		}
		case WM_MOUSELEAVE:
			self->m_hot = -1;
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
};

// ============================================================
// Custom ListView (detail/report view with vertical scrollbar)
// ============================================================
class CustomListView
{
public:
	struct Column { std::wstring text; int width; };

	void Create(HWND hParent, HINSTANCE hInst, HFONT hFont, HFONT hFontBold)
	{
		m_hFont = hFont;
		m_hFontBold = hFontBold;
		m_hInst = hInst;
		m_rowHeight = Dpi::Scale(Layout::ListRowHeight);
		m_headerHeight = Dpi::Scale(Layout::ListHeaderHeight);
		m_scrollbarWidth = Dpi::Scale(Layout::ScrollbarWidth);
		m_cellPadding = Dpi::Scale(Layout::ListCellPadding);

		static bool registered = false;
		if (!registered)
		{
			WNDCLASSW wc = {};
			wc.lpfnWndProc = WndProc;
			wc.hInstance = hInst;
			wc.lpszClassName = L"SysMateCustomLV";
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.style = CS_DBLCLKS;
			RegisterClassW(&wc);
			registered = true;
		}

		m_hWnd = CreateWindowExW(0, L"SysMateCustomLV", L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
			0, 0, 100, 100, hParent, nullptr, hInst, this);
	}

	HWND GetHWND() const { return m_hWnd; }

	void AddColumn(const WCHAR* text, int width)
	{
		m_columns.push_back({text, width});
	}

	int AddItem(const WCHAR* text)
	{
		std::vector<std::wstring> row;
		row.push_back(text);
		for (size_t i = 1; i < m_columns.size(); i++) row.push_back(L"");
		m_rows.push_back(row);
		m_itemData.push_back(0);
		UpdateScrollInfo();
		return (int)m_rows.size() - 1;
	}

	void SetItemText(int item, int subItem, const WCHAR* text)
	{
		if (item < 0 || item >= (int)m_rows.size()) return;
		if (subItem < 0 || subItem >= (int)m_columns.size()) return;
		while ((int)m_rows[item].size() <= subItem) m_rows[item].push_back(L"");
		m_rows[item][subItem] = text;
	}

	void SetItemData(int item, LPARAM data)
	{
		if (item >= 0 && item < (int)m_itemData.size()) m_itemData[item] = data;
	}

	LPARAM GetItemData(int item) const
	{
		if (item >= 0 && item < (int)m_itemData.size()) return m_itemData[item];
		return 0;
	}

	std::wstring GetItemText(int item, int subItem) const
	{
		if (item < 0 || item >= (int)m_rows.size()) return L"";
		if (subItem < 0 || subItem >= (int)m_rows[item].size()) return L"";
		return m_rows[item][subItem];
	}

	void DeleteAllItems()
	{
		m_rows.clear();
		m_itemData.clear();
		m_selected = -1;
		m_scrollPos = 0;
		UpdateScrollInfo();
	}

	int GetItemCount() const { return (int)m_rows.size(); }
	int GetSelected() const { return m_selected; }

	void SetSelected(int idx)
	{
		if (idx != m_selected)
		{
			m_selected = idx;
			EnsureVisible(idx);
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void SetRedraw(bool redraw)
	{
		m_redrawEnabled = redraw;
		if (redraw)
		{
			UpdateScrollInfo();
			InvalidateRect(m_hWnd, nullptr, FALSE);
		}
	}

	void Invalidate() { InvalidateRect(m_hWnd, nullptr, FALSE); }

	using SelectionCallback = std::function<void(int)>;
	using ContextMenuCallback = std::function<void(HWND, int, int)>;
	void SetSelectionCallback(SelectionCallback cb) { m_onSelect = cb; }
	void SetContextMenuCallback(ContextMenuCallback cb) { m_onContextMenu = cb; }

private:
	HWND m_hWnd = nullptr;
	HINSTANCE m_hInst = nullptr;
	HFONT m_hFont = nullptr;
	HFONT m_hFontBold = nullptr;
	std::vector<Column> m_columns;
	std::vector<std::vector<std::wstring>> m_rows;
	std::vector<LPARAM> m_itemData;
	int m_selected = -1;
	int m_scrollPos = 0;
	int m_rowHeight = 22;
	int m_headerHeight = 24;
	bool m_redrawEnabled = true;
	int m_scrollbarWidth = 14;
	int m_cellPadding = 6;
	bool m_scrollbarDragging = false;
	int m_scrollbarDragOffset = 0;
	int m_hot = -1;
	int m_headerDragCol = -1;
	int m_headerDragX = 0;

	SelectionCallback m_onSelect;
	ContextMenuCallback m_onContextMenu;

	int VisibleRows() const
	{
		RECT rc;
		GetClientRect(m_hWnd, &rc);
		return std::max(1, static_cast<int>(rc.bottom - m_headerHeight) / m_rowHeight);
	}

	int MaxScroll() const
	{
		return std::max(0, (int)m_rows.size() - VisibleRows());
	}

	void UpdateScrollInfo()
	{
		m_scrollPos = std::clamp(m_scrollPos, 0, MaxScroll());
	}

	void EnsureVisible(int idx)
	{
		if (idx < 0) return;
		if (idx < m_scrollPos) m_scrollPos = idx;
		int vis = VisibleRows();
		if (idx >= m_scrollPos + vis) m_scrollPos = idx - vis + 1;
		UpdateScrollInfo();
	}

	int HitTestRow(int y) const
	{
		if (y < m_headerHeight) return -1;
		int row = (y - m_headerHeight) / m_rowHeight + m_scrollPos;
		if (row >= (int)m_rows.size()) return -1;
		return row;
	}

	int HitTestHeaderEdge(int x) const
	{
		const int hitZone = Dpi::Scale(Layout::HeaderEdgeHitZone);
		int cx = 0;
		for (int i = 0; i < (int)m_columns.size(); i++)
		{
			cx += m_columns[i].width;
			if (x >= cx - hitZone && x <= cx + hitZone) return i;
		}
		return -1;
	}

	RECT GetScrollbarThumbRect() const
	{
		RECT rc;
		GetClientRect(m_hWnd, &rc);
		int sbX = rc.right - m_scrollbarWidth;
		int sbTop = m_headerHeight;
		int sbH = rc.bottom - sbTop;
		int total = (int)m_rows.size();
		int vis = VisibleRows();
		if (total <= vis) return {sbX, sbTop, rc.right, rc.bottom};
		int thumbH = std::max(Dpi::Scale(Layout::ScrollbarThumbMin), sbH * vis / total);
		int trackH = sbH - thumbH;
		int thumbY = sbTop + (MaxScroll() > 0 ? m_scrollPos * trackH / MaxScroll() : 0);
		return {sbX, thumbY, rc.right, thumbY + thumbH};
	}

	bool NeedsScrollbar() const { return (int)m_rows.size() > VisibleRows(); }

	void OnPaint()
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(m_hWnd, &ps);
		RECT rc;
		GetClientRect(m_hWnd, &rc);

		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
		HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

		// Background
		FillRect(memDC, &rc, Dark::BrushControl());

		SelectObject(memDC, m_hFont);
		SetBkMode(memDC, TRANSPARENT);

		int contentRight = NeedsScrollbar() ? rc.right - m_scrollbarWidth : rc.right;

		// Draw header
		{
			RECT hdrRc = {0, 0, contentRight, m_headerHeight};
			FillRect(memDC, &hdrRc, Dark::BrushHeader());
			int cx = 0;
			SelectObject(memDC, m_hFontBold ? m_hFontBold : m_hFont);
			SetTextColor(memDC, Dark::TextDim);
			for (int i = 0; i < (int)m_columns.size(); i++)
			{
				RECT tr = {cx + m_cellPadding, 0, cx + m_columns[i].width, m_headerHeight};
				DrawTextExt(memDC, m_columns[i].text.c_str(), -1, tr, TextAlign::Left | TextAlign::VCenter | TextAlign::Ellipsis);
				cx += m_columns[i].width;
				// Column separator
				HPEN pen = CreatePen(PS_SOLID, 1, Dark::Border);
				HPEN oldPen = (HPEN)SelectObject(memDC, pen);
				MoveToEx(memDC, cx - 1, 0, nullptr);
				LineTo(memDC, cx - 1, m_headerHeight);
				SelectObject(memDC, oldPen);
				DeleteObject(pen);
			}
			// Header bottom border
			HPEN pen = CreatePen(PS_SOLID, 1, Dark::Border);
			HPEN oldPen = (HPEN)SelectObject(memDC, pen);
			MoveToEx(memDC, 0, m_headerHeight - 1, nullptr);
			LineTo(memDC, contentRight, m_headerHeight - 1);
			SelectObject(memDC, oldPen);
			DeleteObject(pen);
		}

		// Draw rows or empty text
		SelectObject(memDC, m_hFont);
		if (m_rows.empty())
		{
			SetTextColor(memDC, Dark::TextDim);
			RECT emptyRc = {0, m_headerHeight, contentRight, rc.bottom};
			DrawTextExt(memDC, L"Empty", -1, emptyRc, TextAlign::Center | TextAlign::VCenter);
		}
		int vis = VisibleRows();
		for (int vi = 0; vi < vis; vi++)
		{
			int rowIdx = m_scrollPos + vi;
			if (rowIdx >= (int)m_rows.size()) break;

			int y = m_headerHeight + vi * m_rowHeight;
			RECT rowRc = {0, y, contentRight, y + m_rowHeight};

			if (rowIdx == m_selected)
			{
				FillRect(memDC, &rowRc, Dark::BrushSelected());
				SetTextColor(memDC, Dark::TextSel);
			}
			else if (rowIdx == m_hot)
			{
				HBRUSH hbr = CreateSolidBrush(Dark::BgHot);
				FillRect(memDC, &rowRc, hbr);
				DeleteObject(hbr);
				SetTextColor(memDC, Dark::Text);
			}
			else
			{
				COLORREF bg = (rowIdx % 2 == 1) ? RGB(35, 35, 38) : Dark::BgControl;
				HBRUSH hbr = CreateSolidBrush(bg);
				FillRect(memDC, &rowRc, hbr);
				DeleteObject(hbr);
				SetTextColor(memDC, Dark::Text);
			}

			int cx = 0;
			for (int c = 0; c < (int)m_columns.size() && c < (int)m_rows[rowIdx].size(); c++)
			{
				RECT tr = {cx + m_cellPadding, y, cx + m_columns[c].width - m_cellPadding / 2, y + m_rowHeight};
				DrawTextExt(memDC, m_rows[rowIdx][c].c_str(), -1, tr,
					TextAlign::Left | TextAlign::VCenter | TextAlign::Ellipsis);
				cx += m_columns[c].width;
			}
		}

		// Draw scrollbar
		if (NeedsScrollbar())
		{
			RECT sbRc = {rc.right - m_scrollbarWidth, m_headerHeight, rc.right, rc.bottom};
			HBRUSH sbBg = CreateSolidBrush(RGB(35, 35, 38));
			FillRect(memDC, &sbRc, sbBg);
			DeleteObject(sbBg);

			RECT thumbRc = GetScrollbarThumbRect();
			HBRUSH thumbBr = CreateSolidBrush(RGB(80, 80, 85));
			// Rounded-ish thumb: just fill
			FillRect(memDC, &thumbRc, thumbBr);
			DeleteObject(thumbBr);
		}

		BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
		SelectObject(memDC, oldBmp);
		DeleteObject(memBmp);
		DeleteDC(memDC);

		EndPaint(m_hWnd, &ps);
	}

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		CustomListView* self = nullptr;
		if (msg == WM_NCCREATE)
		{
			auto* cs = (CREATESTRUCT*)lParam;
			self = (CustomListView*)cs->lpCreateParams;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
		}
		else
		{
			self = (CustomListView*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		}
		if (!self) return DefWindowProc(hWnd, msg, wParam, lParam);

		switch (msg)
		{
		case WM_PAINT:
			if (self->m_redrawEnabled) self->OnPaint();
			else { PAINTSTRUCT ps; BeginPaint(hWnd, &ps); EndPaint(hWnd, &ps); }
			return 0;
		case WM_ERASEBKGND: return 1;

		case WM_LBUTTONDOWN:
		{
			SetFocus(hWnd);
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);

			// Header column resize
			if (y < self->m_headerHeight)
			{
				int col = self->HitTestHeaderEdge(x);
				if (col >= 0)
				{
					self->m_headerDragCol = col;
					self->m_headerDragX = x;
					SetCapture(hWnd);
					return 0;
				}
			}

			// Scrollbar
			RECT rc;
			GetClientRect(hWnd, &rc);
			if (self->NeedsScrollbar() && x >= rc.right - self->m_scrollbarWidth)
			{
				RECT thumb = self->GetScrollbarThumbRect();
				if (y >= thumb.top && y <= thumb.bottom)
				{
					self->m_scrollbarDragging = true;
					self->m_scrollbarDragOffset = y - thumb.top;
					SetCapture(hWnd);
				}
				else
				{
					// Page up/down
					if (y < thumb.top) self->m_scrollPos = std::max(0, self->m_scrollPos - self->VisibleRows());
					else self->m_scrollPos = std::min(self->MaxScroll(), self->m_scrollPos + self->VisibleRows());
					InvalidateRect(hWnd, nullptr, FALSE);
				}
				return 0;
			}

			// Row selection
			int row = self->HitTestRow(y);
			if (row >= 0 && row != self->m_selected)
			{
				self->m_selected = row;
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onSelect) self->m_onSelect(row);
			}
			return 0;
		}

		case WM_LBUTTONUP:
			if (self->m_scrollbarDragging || self->m_headerDragCol >= 0)
			{
				self->m_scrollbarDragging = false;
				self->m_headerDragCol = -1;
				ReleaseCapture();
			}
			return 0;

		case WM_MOUSEMOVE:
		{
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			if (self->m_headerDragCol >= 0)
			{
				int delta = x - self->m_headerDragX;
				int& w = self->m_columns[self->m_headerDragCol].width;
				w = std::max(Dpi::Scale(Layout::MinColumnWidth), w + delta);
				self->m_headerDragX = x;
				InvalidateRect(hWnd, nullptr, FALSE);
				return 0;
			}
			if (self->m_scrollbarDragging)
			{
				RECT rc;
				GetClientRect(hWnd, &rc);
				int sbH = rc.bottom - self->m_headerHeight;
				int total = (int)self->m_rows.size();
				int vis = self->VisibleRows();
				int thumbH = std::max(Dpi::Scale(Layout::ScrollbarThumbMin), sbH * vis / total);
				int trackH = sbH - thumbH;
				if (trackH > 0)
				{
					int thumbY = y - self->m_scrollbarDragOffset - self->m_headerHeight;
					self->m_scrollPos = std::clamp(thumbY * self->MaxScroll() / trackH, 0, self->MaxScroll());
					InvalidateRect(hWnd, nullptr, FALSE);
				}
				return 0;
			}
			// Hot tracking
			RECT rc;
			GetClientRect(hWnd, &rc);
			if (x < rc.right - self->m_scrollbarWidth)
			{
				int row = self->HitTestRow(y);
				if (row != self->m_hot)
				{
					self->m_hot = row;
					InvalidateRect(hWnd, nullptr, FALSE);
					TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
					TrackMouseEvent(&tme);
				}
			}
			// Cursor for header edge
			if (y < self->m_headerHeight && self->HitTestHeaderEdge(x) >= 0)
				SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
			else
				SetCursor(LoadCursor(nullptr, IDC_ARROW));
			return 0;
		}

		case WM_MOUSELEAVE:
			self->m_hot = -1;
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;

		case WM_MOUSEWHEEL:
		{
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
			self->m_scrollPos -= delta / 40;
			self->m_scrollPos = std::clamp(self->m_scrollPos, 0, self->MaxScroll());
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}

		case WM_KEYDOWN:
			if (wParam == VK_UP && self->m_selected > 0)
			{
				self->m_selected--;
				self->EnsureVisible(self->m_selected);
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onSelect) self->m_onSelect(self->m_selected);
			}
			else if (wParam == VK_DOWN && self->m_selected < (int)self->m_rows.size() - 1)
			{
				self->m_selected++;
				self->EnsureVisible(self->m_selected);
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onSelect) self->m_onSelect(self->m_selected);
			}
			else if (wParam == VK_PRIOR)
			{
				self->m_scrollPos = std::max(0, self->m_scrollPos - self->VisibleRows());
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if (wParam == VK_NEXT)
			{
				self->m_scrollPos = std::min(self->MaxScroll(), self->m_scrollPos + self->VisibleRows());
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if (wParam == VK_HOME)
			{
				self->m_selected = 0;
				self->m_scrollPos = 0;
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onSelect) self->m_onSelect(self->m_selected);
			}
			else if (wParam == VK_END && !self->m_rows.empty())
			{
				self->m_selected = (int)self->m_rows.size() - 1;
				self->EnsureVisible(self->m_selected);
				InvalidateRect(hWnd, nullptr, FALSE);
				if (self->m_onSelect) self->m_onSelect(self->m_selected);
			}
			else if (wParam == VK_F5)
			{
				// Bubble to parent
				SendMessage(GetParent(GetParent(hWnd)), msg, wParam, lParam);
			}
			return 0;

		case WM_CONTEXTMENU:
		{
			if (self->m_onContextMenu && self->m_selected >= 0)
			{
				int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
				self->m_onContextMenu(hWnd, x, y);
			}
			return 0;
		}

		case WM_SIZE:
			self->UpdateScrollInfo();
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
};
