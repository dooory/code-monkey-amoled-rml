#include "title_bar.hpp"

#include <RobloxModLoader/logger/logger.hpp>

#include <Windows.h>
#include <dwmapi.h>

#include <thread>

#pragma comment(lib, "dwmapi.lib")

RML_LOG_SCOPE("StudioTheme")

namespace studio_theme
{
	// Wakes the watcher thread to recolor every window after the palette changes.
	static constexpr UINT k_recolor_message = WM_APP + 1;

	static COLORREF colorref(const Color& color)
	{
		const auto rgb = rgb_key(color);
		return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	}

	static void CALLBACK on_window_shown(HWINEVENTHOOK, DWORD, const HWND window, const LONG object, const LONG child, DWORD, DWORD)
	{
		if (object != OBJID_WINDOW || child != CHILDID_SELF)
			return;
		if (const auto* instance = TitleBar::instance())
			instance->recolor(window);
	}

	TitleBar::~TitleBar()
	{
		uninstall();
	}

	void TitleBar::install()
	{
		if (m_thread.joinable())
			return;

		// Not waited on: install() may run under the loader lock, which holds new threads at startup.
		s_instance.store(this, std::memory_order_release);
		m_thread = std::jthread{[this] {
			MSG message;
			PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // creates the thread's queue
			m_thread_id.store(GetCurrentThreadId());
			run();
		}};
	}

	void TitleBar::uninstall()
	{
		if (!m_thread.joinable())
			return;

		while (m_thread_id.load() == 0)
			std::this_thread::yield();
		PostThreadMessageW(m_thread_id.load(), WM_QUIT, 0, 0);
		m_thread.join();
		m_thread_id.store(0);
		s_instance.store(nullptr, std::memory_order_release);
	}

	void TitleBar::set_palette(std::shared_ptr<const Palette> palette)
	{
		{
			std::scoped_lock lock{m_mutex};
			m_palette = std::move(palette);
		}
		// Before the thread has a queue, its first recolor_all() picks the palette up instead.
		if (const auto thread = m_thread_id.load())
			PostThreadMessageW(thread, k_recolor_message, 0, 0);
	}

	TitleBar* TitleBar::instance()
	{
		return s_instance.load(std::memory_order_acquire);
	}

	void TitleBar::run()
	{
		const HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, on_window_shown, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
		if (!hook)
			RML_WARN("Watching for new windows failed ({}); only windows open now get a themed title bar", GetLastError());
		recolor_all();

		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			if (message.message == k_recolor_message)
				recolor_all();
			DispatchMessageW(&message);
		}

		if (hook)
			UnhookWinEvent(hook);

		// Hand the windows back with Windows' own title bar.
		{
			std::scoped_lock lock{m_mutex};
			m_palette.reset();
		}
		recolor_all();
	}

	void TitleBar::recolor_all() const
	{
		EnumWindows(
		    [](const HWND window, const LPARAM self) -> BOOL {
			    DWORD process = 0;
			    GetWindowThreadProcessId(window, &process);
			    if (process == GetCurrentProcessId())
				    reinterpret_cast<const TitleBar*>(self)->recolor(window);
			    return TRUE;
		    },
		    reinterpret_cast<LPARAM>(this));
	}

	void TitleBar::recolor(void* const handle) const
	{
		const auto window = static_cast<HWND>(handle);
		if (GetAncestor(window, GA_ROOT) != window || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) != WS_CAPTION)
			return;

		const auto active = palette();
		const BOOL dark = active && active->theme().base == Base::Dark;
		const COLORREF caption = active ? colorref(active->theme().role(Role::Surface)) : DWMWA_COLOR_DEFAULT;
		const COLORREF text = active ? colorref(active->theme().role(Role::Text)) : DWMWA_COLOR_DEFAULT;
		const COLORREF border = active ? colorref(active->theme().role(Role::Border)) : DWMWA_COLOR_DEFAULT;

		DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
		DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
		DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
		DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
	}

	std::shared_ptr<const Palette> TitleBar::palette() const
	{
		std::scoped_lock lock{m_mutex};
		return m_palette;
	}
}
