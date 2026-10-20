#pragma once

#include "palette.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace studio_theme
{
	// Colors the Windows-drawn title bar of every Studio window (main window, dialogs, floating
	// panels) with the palette's Surface, Text and Border roles. DWM keeps the colors per window,
	// so a background thread watches for windows being shown and colors each one once.
	class TitleBar
	{
	public:
		TitleBar() = default;
		~TitleBar();

		TitleBar(const TitleBar&) = delete;
		TitleBar& operator=(const TitleBar&) = delete;

		void install();
		void uninstall();
		void set_palette(std::shared_ptr<const Palette> palette);

		// For the window-shown callback in title_bar.cpp.
		[[nodiscard]] static TitleBar* instance();
		void recolor(void* window) const;

	private:
		void run();
		void recolor_all() const;
		[[nodiscard]] std::shared_ptr<const Palette> palette() const;

		mutable std::mutex m_mutex;
		std::shared_ptr<const Palette> m_palette;
		std::jthread m_thread;
		std::atomic<unsigned long> m_thread_id{0};

		static inline std::atomic<TitleBar*> s_instance{nullptr};
	};
}
