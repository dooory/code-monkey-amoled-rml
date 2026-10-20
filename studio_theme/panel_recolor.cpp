#include "panel_recolor.hpp"

#include <RobloxModLoader/hooking/hooking.hpp>
#include <RobloxModLoader/logger/logger.hpp>
#include <RobloxModLoader/render/render.hpp>

#include <Windows.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

RML_LOG_SCOPE("StudioTheme")

using Microsoft::WRL::ComPtr;

namespace studio_theme
{
	namespace
	{
		constexpr std::size_t k_max_knots = 8;
		constexpr std::size_t k_max_hues = 8;
		constexpr std::uint64_t k_stale_presents = 600;
		constexpr std::string_view k_marker_pass = "StudioTheme.ViewportMarker";

		// Follows Palette::map (palette.cpp) on the GPU, minus exact-color overrides, narrowed so
		// thumbnails in the panels keep their colors (see neutral_weight and upload_palette).
		constexpr const char* k_shader = R"(
Texture2D source : register(t0);
SamplerState point_sampler : register(s0);

cbuffer Mapping : register(b0)
{
	float4 knots[8];      // x: stock lightness, yzw: target OKLab
	float4 hue_stock[8];  // xyz: stock OKLCh
	float4 hue_target[8]; // xyz: target OKLCh
	float4 limits;        // x: knot count, y: hue count
	float4 reach;         // x: hue reach in degrees
	float4 knot_tint[8];  // xy: stock OKLab a, b
};

struct VsOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };

VsOut vs_main(uint id : SV_VertexID)
{
	VsOut o;
	o.uv = float2((id << 1) & 2, id & 2);
	o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return o;
}

float3 to_linear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 to_gamma(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }

float3 to_oklab(float3 srgb)
{
	float3 c = to_linear(srgb);
	float3 lms = float3(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b,
	                    0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b,
	                    0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
	lms = pow(max(lms, 0), 1.0 / 3.0);
	return float3(0.2104542553 * lms.x + 0.7936177850 * lms.y - 0.0040720468 * lms.z,
	              1.9779984951 * lms.x - 2.4285922050 * lms.y + 0.4505937099 * lms.z,
	              0.0259040371 * lms.x + 0.7827717662 * lms.y - 0.8086757660 * lms.z);
}

float3 linear_rgb(float3 lab)
{
	float3 lms = float3(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z,
	                    lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z,
	                    lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z);
	lms = lms * lms * lms;
	return float3(4.0767416621 * lms.x - 3.3077115913 * lms.y + 0.2309699292 * lms.z,
	             -1.2684380046 * lms.x + 2.6097574011 * lms.y - 0.3413193965 * lms.z,
	             -0.0041960863 * lms.x - 0.7034186147 * lms.y + 1.7076147010 * lms.z);
}

bool in_gamut(float3 rgb) { return all(rgb >= -1e-4) && all(rgb <= 1.0001); }

float3 from_oklab(float3 lab)
{
	lab.x = saturate(lab.x);
	float3 rgb = linear_rgb(lab);
	if (!in_gamut(rgb))
	{
		float inside = 0.0;
		float outside = 1.0;
		[loop] for (int step = 0; step < 16; ++step)
		{
			float scale = (inside + outside) * 0.5;
			if (in_gamut(linear_rgb(float3(lab.x, lab.yz * scale))))
				inside = scale;
			else
				outside = scale;
		}
		rgb = linear_rgb(float3(lab.x, lab.yz * inside));
	}
	return saturate(to_gamma(saturate(rgb)));
}

float3 to_lch(float3 lab)
{
	float hue = degrees(atan2(lab.z, lab.y));
	return float3(lab.x, length(lab.yz), hue < 0 ? hue + 360 : hue);
}

float3 to_lab(float3 lch)
{
	float r = radians(lch.z);
	return float3(lch.x, lch.y * cos(r), lch.y * sin(r));
}

float hue_delta(float from, float to)
{
	float delta = fmod(to - from, 360.0);
	if (delta > 180.0) delta -= 360.0;
	if (delta < -180.0) delta += 360.0;
	return delta;
}

float3 ramp(float l)
{
	int count = (int)limits.x;
	if (l <= knots[0].x)
		return float3(knots[0].y + (l - knots[0].x), knots[0].zw);
	[loop] for (int i = 1; i < count; ++i)
	{
		if (l <= knots[i].x)
		{
			float t = (l - knots[i - 1].x) / (knots[i].x - knots[i - 1].x);
			return lerp(knots[i - 1].yzw, knots[i].yzw, t);
		}
	}
	float4 last = knots[count - 1];
	return float3(last.y + (l - last.x), last.zw);
}

// The stock neutrals' slight blue tint at a lightness, interpolated between the ramp's knots and
// fading out toward pure black and pure white.
float2 stock_tint(float l)
{
	int count = (int)limits.x;
	if (l <= knots[0].x)
		return knot_tint[0].xy * saturate(l / knots[0].x);
	[loop] for (int i = 1; i < count; ++i)
	{
		if (l <= knots[i].x)
			return lerp(knot_tint[i - 1].xy, knot_tint[i].xy, (l - knots[i - 1].x) / (knots[i].x - knots[i - 1].x));
	}
	float4 last = knots[count - 1];
	return knot_tint[count - 1].xy * saturate((1.0 - l) / (1.0 - last.x));
}

// Panels also show thumbnails, and a pixel alone can't say whether it's UI or photo. Studio's
// greys all sit on one curve (a slight blue that varies with lightness), and its anti-aliasing
// blends stay near it; photos almost never do. So only pixels within a hair of that curve are
// treated as UI grey, and everything else in a thumbnail keeps its own color.
float curve_weight(float3 lab)
{
	return 1.0 - smoothstep(0.003, 0.008, length(lab.yz - stock_tint(lab.x)));
}

// Averaged over a 5x5 neighbourhood: a dithered gradient (the sign-in backdrop) has pixels on
// both sides of the curve, and judging each alone turns it grainy.
float neutral_weight(int2 pixel)
{
	float sum = 0;
	[unroll] for (int y = -2; y <= 2; y += 2)
		[unroll] for (int x = -2; x <= 2; x += 2)
			sum += curve_weight(to_oklab(source.Load(int3(pixel + int2(x, y), 0)).rgb));
	return sum / 9.0;
}

float4 ps_main(VsOut input) : SV_Target
{
	float4 color = source.Sample(point_sampler, input.uv);
	float3 lab = to_oklab(color.rgb);
	float3 lch = to_lch(lab);

	float neutral = neutral_weight(int2(input.position.xy));
	// Keeps the pixel's small offset from the curve, so Studio's greys land exactly on the theme's.
	float3 neutral_mapped = ramp(lab.x) + float3(0, lab.yz - stock_tint(lab.x));
	float3 mapped = lerp(lab, neutral_mapped, neutral);

	// Blends every accent anchor in reach rather than snapping to the nearest, so hues between
	// two anchors (red and orange) don't flip from one target to the other. The fourth power keeps
	// a token on its own anchor's target when a neighbouring anchor is also in reach.
	float3 shifted_sum = 0;
	float weight_sum = 0;
	float strength = 0;
	[loop] for (int i = 0; i < (int)limits.y; ++i)
	{
		float3 stock = hue_stock[i].xyz;
		float3 target = hue_target[i].xyz;
		float delta = hue_delta(stock.z, lch.z);
		// Only colors near an accent token in hue, lightness and saturation; a sky or a photo keeps its own colors.
		float weight = (1.0 - smoothstep(reach.x * 0.5, reach.x, abs(delta)))
		    * (1.0 - smoothstep(0.04, 0.1, abs(lch.x - stock.x)))
		    * (1.0 - smoothstep(0.04, 0.09, abs(lch.y - stock.y)));
		if (weight <= 0.0)
			continue;
		float chroma = stock.y > 0 ? target.y * lch.y / stock.y : target.y;
		float share = weight * weight * weight * weight;
		shifted_sum += share * to_lab(float3(target.x + (lch.x - stock.x), chroma, target.z + delta));
		weight_sum += share;
		strength = max(strength, weight);
	}
	if (weight_sum > 0.0)
		mapped = lerp(mapped, shifted_sum / weight_sum, strength * (1.0 - neutral));
	return float4(from_oklab(mapped), color.a);
}
)";

		struct alignas(16) Mapping
		{
			float knots[k_max_knots][4];
			float hue_stock[k_max_hues][4];
			float hue_target[k_max_hues][4];
			float limits[4];
			float reach[4];
			float knot_tint[k_max_knots][4];
		};

		struct SwapEntry
		{
			ComPtr<ID3D11Texture2D> copy;
			ComPtr<ID3D11ShaderResourceView> view;
			UINT width{};
			UINT height{};
			DXGI_FORMAT format{};
			bool viewport{};
			std::uint64_t last_present{};
		};

		bool remappable(const DXGI_FORMAT format)
		{
			return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_R10G10B10A2_UNORM;
		}

		// Set by the render graph at FrameEnd, which only runs for the 3D viewport; the next Present
		// on the same thread is that viewport's swapchain.
		thread_local bool t_viewport_frame{};
		thread_local bool t_in_present{};

		class ViewportMarker final : public rml::render::IRenderPass
		{
		public:
			[[nodiscard]] std::string get_name() const override
			{
				return std::string{k_marker_pass};
			}

			[[nodiscard]] rml::render::InjectionPoint get_injection_point() const override
			{
				return rml::render::InjectionPoint::at(rml::render::FramePoint::FrameEnd);
			}

			void render(const rml::render::RenderContext&) override
			{
				t_viewport_frame = true;
			}
		};
	}

	struct PanelRecolor::State
	{
		std::mutex mutex;
		std::shared_ptr<const Palette> palette;
		bool palette_dirty{true};

		ComPtr<ID3D11Device1> device;
		ComPtr<ID3D11DeviceContext1> context;
		ComPtr<ID3D11Multithread> multithread;
		ComPtr<ID3DDeviceContextState> isolated;
		ComPtr<ID3D11VertexShader> vertex_shader;
		ComPtr<ID3D11PixelShader> pixel_shader;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11Buffer> constants;
		bool broken{};

		std::unordered_map<IDXGISwapChain*, SwapEntry> swaps;
		std::uint64_t presents{};

		bool ensure_device(IDXGISwapChain* swap);
		void upload_palette();
		void remap(IDXGISwapChain* swap, SwapEntry& entry);
		void on_present(IDXGISwapChain* swap);
	};

	static std::atomic<PanelRecolor::State*> s_state{nullptr};

	static ComPtr<ID3DBlob> compile(const char* entry, const char* target)
	{
		ComPtr<ID3DBlob> code;
		ComPtr<ID3DBlob> errors;
		if (FAILED(D3DCompile(k_shader, std::strlen(k_shader), "panel_recolor", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)))
		{
			RML_ERROR("Compiling the panel recolor {} failed: {}", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
			return nullptr;
		}
		return code;
	}

	bool PanelRecolor::State::ensure_device(IDXGISwapChain* const swap)
	{
		ComPtr<ID3D11Device> current;
		if (FAILED(swap->GetDevice(IID_PPV_ARGS(&current))))
			return false;
		if (device && current.Get() == static_cast<ID3D11Device*>(device.Get()))
			return true;
		if (broken)
			return false;

		// First use, or Studio recreated its device: drop everything made on the old one.
		swaps.clear();
		device.Reset();
		context.Reset();
		multithread.Reset();
		isolated.Reset();
		vertex_shader.Reset();
		pixel_shader.Reset();
		sampler.Reset();
		constants.Reset();
		broken = true;
		if (FAILED(current.As(&device)))
		{
			RML_WARN("The panel recolor is off: Studio's device has no Direct3D 11.1 interface");
			return false;
		}
		ComPtr<ID3D11DeviceContext> immediate;
		device->GetImmediateContext(&immediate);
		immediate.As(&context);
		immediate.As(&multithread);

		const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
		if (FAILED(device->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &isolated)))
		{
			RML_WARN("The panel recolor is off: cannot create a separate device context state");
			return false;
		}

		const auto vertex = compile("vs_main", "vs_5_0");
		const auto pixel = compile("ps_main", "ps_5_0");
		if (!vertex || !pixel)
			return false;
		device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &vertex_shader);
		device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &pixel_shader);

		D3D11_SAMPLER_DESC sampler_desc{};
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
		device->CreateSamplerState(&sampler_desc, &sampler);

		D3D11_BUFFER_DESC buffer_desc{};
		buffer_desc.ByteWidth = sizeof(Mapping);
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		device->CreateBuffer(&buffer_desc, nullptr, &constants);

		broken = !vertex_shader || !pixel_shader || !sampler || !constants;
		if (broken)
			RML_WARN("The panel recolor is off: creating its GPU resources failed");
		else
			RML_INFO("Panel recolor ready");
		palette_dirty = true;
		return !broken;
	}

	void PanelRecolor::State::upload_palette()
	{
		palette_dirty = false;
		if (!palette)
			return;

		const auto& knots = palette->ramp_knots();
		const auto& hues = palette->hue_anchors();
		Mapping mapping{};
		const auto knot_count = std::min(knots.size(), k_max_knots);
		std::size_t hue_count = 0;
		for (std::size_t i = 0; i < knot_count; ++i)
		{
			const auto& knot = knots[i];
			mapping.knots[i][0] = static_cast<float>(knot.stock.l);
			mapping.knots[i][1] = static_cast<float>(knot.target.l);
			mapping.knots[i][2] = static_cast<float>(knot.target.a);
			mapping.knots[i][3] = static_cast<float>(knot.target.b);
			mapping.knot_tint[i][0] = static_cast<float>(knot.stock.a);
			mapping.knot_tint[i][1] = static_cast<float>(knot.stock.b);
		}
		// Only the accent: the panels use Studio's blue for buttons, links and selection, while its
		// red, orange, yellow and green mostly turn up in thumbnails, where shifting them is wrong
		// (an orange butterfly turns yellow). The Qt shell still maps all of them.
		for (const auto& [stock, target, role] : hues)
		{
			if (role != Role::Accent || hue_count == k_max_hues)
				continue;
			const auto i = hue_count++;
			mapping.hue_stock[i][0] = static_cast<float>(stock.l);
			mapping.hue_stock[i][1] = static_cast<float>(stock.c);
			mapping.hue_stock[i][2] = static_cast<float>(stock.h);
			mapping.hue_target[i][0] = static_cast<float>(target.l);
			mapping.hue_target[i][1] = static_cast<float>(target.c);
			mapping.hue_target[i][2] = static_cast<float>(target.h);
		}
		mapping.limits[0] = static_cast<float>(knot_count);
		mapping.limits[1] = static_cast<float>(hue_count);
		mapping.reach[0] = static_cast<float>(Palette::k_hue_reach);
		context->UpdateSubresource(constants.Get(), 0, nullptr, &mapping, 0, 0);
	}

	void PanelRecolor::State::remap(IDXGISwapChain* const swap, SwapEntry& entry)
	{
		ComPtr<ID3D11Texture2D> back;
		if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))))
			return;
		D3D11_TEXTURE2D_DESC desc{};
		back->GetDesc(&desc);
		if (desc.SampleDesc.Count != 1 || !remappable(desc.Format) || !(desc.BindFlags & D3D11_BIND_RENDER_TARGET))
			return;

		if (!entry.copy || entry.width != desc.Width || entry.height != desc.Height || entry.format != desc.Format)
		{
			D3D11_TEXTURE2D_DESC copy_desc = desc;
			copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			copy_desc.Usage = D3D11_USAGE_DEFAULT;
			copy_desc.CPUAccessFlags = 0;
			copy_desc.MiscFlags = 0;
			entry.copy.Reset();
			entry.view.Reset();
			if (FAILED(device->CreateTexture2D(&copy_desc, nullptr, &entry.copy)) || FAILED(device->CreateShaderResourceView(entry.copy.Get(), nullptr, &entry.view)))
				return;
			entry.width = desc.Width;
			entry.height = desc.Height;
			entry.format = desc.Format;
		}

		// The back buffer's render target view is not cached: holding a reference to the back
		// buffer would make Studio's ResizeBuffers fail.
		ComPtr<ID3D11RenderTargetView> target;
		if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &target)))
			return;

		if (multithread)
			multithread->Enter();
		ComPtr<ID3DDeviceContextState> studio_state;
		context->SwapDeviceContextState(isolated.Get(), &studio_state);

		if (palette_dirty)
			upload_palette();
		context->CopyResource(entry.copy.Get(), back.Get());
		const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};
		context->RSSetViewports(1, &viewport);
		context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->PSSetShader(pixel_shader.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
		context->PSSetShaderResources(0, 1, entry.view.GetAddressOf());
		context->PSSetSamplers(0, 1, sampler.GetAddressOf());
		context->Draw(3, 0);

		ID3D11ShaderResourceView* const unbound = nullptr;
		context->PSSetShaderResources(0, 1, &unbound);
		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->SwapDeviceContextState(studio_state.Get(), nullptr);
		if (multithread)
			multithread->Leave();
	}

	void PanelRecolor::State::on_present(IDXGISwapChain* const swap)
	{
		const bool viewport_frame = std::exchange(t_viewport_frame, false);

		std::scoped_lock lock{mutex};
		++presents;
		auto [it, inserted] = swaps.try_emplace(swap);
		auto& entry = it->second;
		if (viewport_frame && !entry.viewport)
		{
			entry.viewport = true;
			RML_INFO("Leaving the 3D viewport's swapchain unrecolored");
		}
		entry.last_present = presents;

		if (presents % k_stale_presents == 0)
		{
			std::erase_if(swaps, [this](const auto& item) {
				return item.second.last_present + k_stale_presents < presents;
			});
		}

		if (!entry.viewport && palette && ensure_device(swap))
			remap(swap, swaps[swap]);
	}

	static HRESULT __stdcall present(IDXGISwapChain* const self, const UINT sync, const UINT flags)
	{
		auto* const state = s_state.load(std::memory_order_acquire);
		if (!state || t_in_present || (flags & DXGI_PRESENT_TEST))
			return rml::Hooking::get_original<&present>()(self, sync, flags);

		t_in_present = true;
		state->on_present(self);
		const auto result = rml::Hooking::get_original<&present>()(self, sync, flags);
		t_in_present = false;
		return result;
	}

	static HRESULT __stdcall present1(IDXGISwapChain1* const self, const UINT sync, const UINT flags, const DXGI_PRESENT_PARAMETERS* const parameters)
	{
		auto* const state = s_state.load(std::memory_order_acquire);
		if (!state || t_in_present || (flags & DXGI_PRESENT_TEST))
			return rml::Hooking::get_original<&present1>()(self, sync, flags, parameters);

		t_in_present = true;
		state->on_present(self);
		const auto result = rml::Hooking::get_original<&present1>()(self, sync, flags, parameters);
		t_in_present = false;
		return result;
	}

	// Present lives in dxgi.dll, so its address comes from a throwaway swapchain's vtable.
	static bool find_present(void*& present_fn, void*& present1_fn)
	{
		WNDCLASSA window_class{};
		window_class.lpfnWndProc = DefWindowProcA;
		window_class.hInstance = GetModuleHandleA(nullptr);
		window_class.lpszClassName = "StudioThemePresentProbe";
		RegisterClassA(&window_class);
		const HWND window = CreateWindowA(window_class.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);

		DXGI_SWAP_CHAIN_DESC desc{};
		desc.BufferCount = 1;
		desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.OutputWindow = window;
		desc.SampleDesc.Count = 1;
		desc.Windowed = TRUE;

		bool found = false;
		{
			ComPtr<IDXGISwapChain> swap;
			ComPtr<ID3D11Device> device;
			if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap, &device, nullptr, nullptr)))
			{
				present_fn = (*reinterpret_cast<void***>(swap.Get()))[8];
				ComPtr<IDXGISwapChain1> swap1;
				present1_fn = SUCCEEDED(swap.As(&swap1)) ? (*reinterpret_cast<void***>(swap1.Get()))[22] : nullptr;
				found = true;
			}
		}
		DestroyWindow(window);
		UnregisterClassA(window_class.lpszClassName, window_class.hInstance);
		return found;
	}

	PanelRecolor::PanelRecolor() :
	    m_state(std::make_unique<State>())
	{
	}

	PanelRecolor::~PanelRecolor()
	{
		uninstall();
	}

	void PanelRecolor::install()
	{
		if (s_state.load())
			return;

		void* present_fn{};
		void* present1_fn{};
		if (!find_present(present_fn, present1_fn))
		{
			RML_WARN("The panel recolor is off: could not find IDXGISwapChain::Present");
			return;
		}

		rml::render::graph().add_pass(std::make_unique<ViewportMarker>());
		s_state.store(m_state.get(), std::memory_order_release);
		rml::Hooking::DetourHookHelper::add<&present>("IDXGISwapChain::Present", present_fn);
		if (present1_fn)
			rml::Hooking::DetourHookHelper::add<&present1>("IDXGISwapChain1::Present1", present1_fn);
	}

	void PanelRecolor::uninstall()
	{
		if (!s_state.exchange(nullptr))
			return;

		rml::Hooking::DetourHookHelper::remove<&present>();
		rml::Hooking::DetourHookHelper::remove<&present1>();
		rml::render::graph().remove_pass(k_marker_pass);
	}

	void PanelRecolor::set_palette(std::shared_ptr<const Palette> palette)
	{
		std::scoped_lock lock{m_state->mutex};
		m_state->palette = std::move(palette);
		m_state->palette_dirty = true;
	}
}
