#pragma once

#include "engine.hpp"
#include "palette.hpp"

#include <RobloxModLoader/qt/qbytearray.hpp>
#include <RobloxModLoader/qt/qstring.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rml::qt
{
	class QWidget;
}

namespace studio_theme
{
	class QtTheme
	{
	public:
		QtTheme(Engine engine, std::function<void(Base)> on_applied);
		~QtTheme();

		QtTheme(const QtTheme&) = delete;
		QtTheme& operator=(const QtTheme&) = delete;

		void install();
		void uninstall();
		void set_palette(std::shared_ptr<const Palette> palette);
		void apply(bool commit);

		[[nodiscard]] bool available() const;

	private:
		struct ParsedTheme
		{
			Theme* theme{};
			std::string stock;
			std::string name;
			std::string applied;
		};

		static void parse_theme_json(Theme* self, const rml::qt::QByteArray& json);
		// Studio sets some stylesheets (the menu bar's, when a place opens) after the theme is
		// applied, with its stock colors written in; this recolors them on the way in.
		static void set_style_sheet(rml::qt::QWidget* self, const rml::qt::QString& css);

		[[nodiscard]] std::string recolor_style_sheet(const std::string& css) const;

		[[nodiscard]] std::string record(Theme* theme, std::string_view stock);
		void apply_on_gui_thread(const std::shared_ptr<const Palette>& palette, bool commit);

		Engine m_engine;
		std::function<void(Base)> m_on_applied;
		mutable std::mutex m_mutex;
		std::vector<ParsedTheme> m_parsed;
		std::shared_ptr<const Palette> m_palette;
		std::atomic<Base> m_last_base{Base::Dark};
		std::atomic<bool> m_installed{false};
		bool m_style_sheet_hooked{};

		static inline std::atomic<QtTheme*> s_instance{nullptr};
	};
}
