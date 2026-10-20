#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/memory/foreign_call.hpp>
#include <RobloxModLoader/qt/qapplication.hpp>
#include <RobloxModLoader/qt/qstring.hpp>
#include <RobloxModLoader/qt/qwidget.hpp>

#include "qt_symbol.hpp"

RML_LOG_SCOPE("StudioThemeDiag")

namespace studio_theme
{
	struct DiagPoint
	{
		int x;
		int y;
	};

	// The Qt functions the dump calls.
	static constexpr QtSymbol k_style_sheet{"_ZNK7QWidget10styleSheetEv", "Qt5Widgets.dll", "?styleSheet@QWidget@@QEBA?AVQString@@XZ"};
	static constexpr QtSymbol k_object_name{"_ZNK7QObject10objectNameEv", "Qt5Core.dll", "?objectName@QObject@@QEBA?AVQString@@XZ"};
	static constexpr QtSymbol k_palette{"_ZNK7QWidget7paletteEv", "Qt5Widgets.dll", "?palette@QWidget@@QEBAAEBVQPalette@@XZ"};
	static constexpr QtSymbol k_brush{"_ZNK8QPalette5brushENS_10ColorGroupENS_9ColorRoleE", "Qt5Gui.dll", "?brush@QPalette@@QEBAAEBVQBrush@@W4ColorGroup@1@W4ColorRole@1@@Z"};
	static constexpr QtSymbol k_color{"_ZNK6QBrush5colorEv", "Qt5Gui.dll", "?color@QBrush@@QEBAAEBVQColor@@XZ"};
	static constexpr QtSymbol k_color_name{"_ZNK6QColor4nameENS_10NameFormatE", "Qt5Gui.dll", "?name@QColor@@QEBA?AVQString@@W4NameFormat@1@@Z"};
	static constexpr QtSymbol k_map_to_global{"_ZNK7QWidget11mapToGlobalERK6QPoint", "Qt5Widgets.dll", "?mapToGlobal@QWidget@@QEBA?AVQPoint@@AEBV2@@Z"};
	static constexpr QtSymbol k_is_visible{"_ZNK7QWidget9isVisibleEv", "Qt5Widgets.dll", "?isVisible@QWidget@@QEBA_NXZ"};

	static std::string qstring_member(const QtSymbol& symbol, const void* self)
	{
		void* fn = resolve(symbol);
		if (!fn)
			return "?";
		rml::qt::QString out;
		rml::memory::call_returning_member(fn, out, self);
		return out.to_utf8();
	}

	static std::string brush_name(const rml::qt::QWidget* widget, int role)
	{
		auto* palette_fn = reinterpret_cast<const void* (*)(const void*)>(resolve(k_palette));
		auto* brush_fn = reinterpret_cast<const void* (*)(const void*, int, int)>(resolve(k_brush));
		auto* color_fn = reinterpret_cast<const void* (*)(const void*)>(resolve(k_color));
		void* name_fn = resolve(k_color_name);
		if (!palette_fn || !brush_fn || !color_fn || !name_fn)
			return "?";
		const void* color = color_fn(brush_fn(palette_fn(widget), 0, role));
		rml::qt::QString out;
		rml::memory::call_returning_member(name_fn, out, color, 0);
		return out.to_utf8();
	}

	void dump_widgets()
	{
		void* map_fn = resolve(k_map_to_global);
		auto* visible_fn = reinterpret_cast<bool (*)(const void*)>(resolve(k_is_visible));
		const auto widgets = rml::qt::QApplication::all_widgets();
		RML_INFO("diag: {} widgets", widgets.size());
		for (auto* widget : widgets)
		{
			const int w = widget->width();
			const int h = widget->height();
			if (w < 200 || h < 4 || (visible_fn && !visible_fn(widget)))
				continue;
			const DiagPoint origin{0, 0};
			DiagPoint global{-1, -1};
			rml::memory::call_returning_member(map_fn, global, widget, &origin);
			auto sheet = qstring_member(k_style_sheet, widget);
			if (sheet.size() > 220)
				sheet.resize(220);
			for (auto& c : sheet)
				if (c == '\n')
					c = ' ';
			RML_INFO("diag: {} '{}' {}x{} at {},{} window={} base={} button={} css=[{}]",
			    widget->class_name(),
			    qstring_member(k_object_name, widget),
			    w,
			    h,
			    global.x,
			    global.y,
			    brush_name(widget, 10),
			    brush_name(widget, 9),
			    brush_name(widget, 1),
			    sheet);
		}
	}
}
