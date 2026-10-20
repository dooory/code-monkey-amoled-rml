#pragma once

#include "palette.hpp"

#include <memory>

namespace studio_theme
{
	// Recolors Studio's Luau-built panels (menu bar, ribbon, Explorer, Start Page). They draw with
	// Foundation tokens compiled into Studio, not the theme document, so QtTheme cannot reach them.
	// Each panel presents through its own DXGI swapchain; this remaps the finished image with the
	// palette's mapping just before Present. The 3D viewport is left alone so game UI keeps its
	// colors: it is the swapchain presented right after the render graph's FrameEnd.
	class PanelRecolor
	{
	public:
		PanelRecolor();
		~PanelRecolor();

		PanelRecolor(const PanelRecolor&) = delete;
		PanelRecolor& operator=(const PanelRecolor&) = delete;

		void install();
		void uninstall();
		void set_palette(std::shared_ptr<const Palette> palette);

		// Defined in panel_recolor.cpp; public only so the Present detours there can name it.
		struct State;

	private:
		std::unique_ptr<State> m_state;
	};
}
