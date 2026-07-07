#include <neotrace/neotrace.h>

#include "neotrace_backend.h"

#include <cinttypes>
#include <cstdio>
#include <string_view>

// the itanium demangler is a toolchain facility, present on every platform whose runtime
// uses that abi (linux, macos, emscripten, mingw/llvm-mingw); plain msvc lacks it, where
// demangle() degrades to returning names raw
#if __has_include(<cxxabi.h>)
#include <cstdlib>
#include <cxxabi.h>
#define NEOTRACE_HAVE_CXA_DEMANGLE 1
#endif

namespace neotrace
{

NEOTRACE_NOINLINE trace trace::current(size_t skip) noexcept
{
	trace t;
	t.count = capture({t.frames, max_frames}, skip + 1); // + 1 drops current() itself
	return t;
}

// graceful-degradation fallbacks for platforms where no backend was selected

#if NEOTRACE_CAPTURE_NONE
size_t capture(std::span<void *>, size_t) noexcept
{
	return 0;
}
#endif

#if NEOTRACE_SYMBOLIZE_NONE
frame_info symbolize(void *address) noexcept
{
	frame_info info;
	info.address = address;
	return info;
}
#endif

std::string demangle(const char *mangled) noexcept
{
	if (mangled == nullptr)
		return {};
#if NEOTRACE_HAVE_CXA_DEMANGLE
	int status = 0;
	char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
	std::string result((status == 0 && demangled != nullptr) ? demangled : mangled); // non-c++ names don't demangle; keep them raw
	std::free(demangled);                                                            // a no-op on the null failure path
	return result;
#else
	return mangled;
#endif
}

namespace
{

std::string_view basename_of(std::string_view path) noexcept
{
	const auto pos = path.find_last_of("/\\");
	return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

void append_hex(std::string &out, uintptr_t value, bool pad = false)
{
	char buf[2 + sizeof(value) * 2 + 1];
	std::snprintf(buf, sizeof(buf), "0x%0*" PRIxPTR, pad ? static_cast<int>(sizeof(value) * 2) : 0, value);
	out += buf;
}

} // namespace

std::string to_string(const frame_info &frame) noexcept
{
	std::string out;
	append_hex(out, reinterpret_cast<uintptr_t>(frame.address), true);
	if (!frame.symbol.empty())
	{
		out += " in ";
		out += frame.symbol;
		if (frame.offset != 0)
		{
			out += " + ";
			append_hex(out, frame.offset);
		}
	}
	if (!frame.module.empty())
	{
		out += " (";
		out += basename_of(frame.module);
		if (frame.symbol.empty() && frame.offset != 0)
		{
			out += " + ";
			append_hex(out, frame.offset);
		}
		out += ')';
	}
	if (!frame.file.empty())
	{
		out += " at ";
		out += frame.file;
		out += ':';
		out += std::to_string(frame.line);
	}
	return out;
}

std::string to_string(std::span<void *const> addresses) noexcept
{
	std::string out;
	for (size_t i = 0; i < addresses.size(); ++i)
	{
		char prefix[24]; // "#" + at most 20 digits of a 64-bit index + " " + nul
		std::snprintf(prefix, sizeof(prefix), "#%-2zu ", i);
		if (i != 0)
			out += '\n';
		out += prefix;
		out += to_string(symbolize(addresses[i]));
	}
	return out;
}

std::span<void *const> trace::addresses() const noexcept
{
	return {frames, count};
}

std::string to_string(const trace &t) noexcept
{
	return to_string(t.addresses());
}

} // namespace neotrace
