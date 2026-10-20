#pragma once

#if defined(__APPLE__)
#include <dlfcn.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace studio_theme
{
	// A Qt function by its Itanium (macOS) and MSVC (Windows) export names.
	struct QtSymbol
	{
		const char* itanium;
		const char* msvc_module;
		const char* msvc;
	};

	inline void* resolve(const QtSymbol& symbol)
	{
#if defined(__APPLE__)
		return dlsym(RTLD_DEFAULT, symbol.itanium);
#else
		const HMODULE module = GetModuleHandleA(symbol.msvc_module);
		return module ? reinterpret_cast<void*>(GetProcAddress(module, symbol.msvc)) : nullptr;
#endif
	}
}
