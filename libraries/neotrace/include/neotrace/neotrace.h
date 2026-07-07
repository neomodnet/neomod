#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// neotrace - minimal stack trace capture and symbolization.
//
// two-step model: capture() grabs raw return addresses (cheap, noexcept, no allocation,
// async-signal-friendly); symbolize() turns an address into names (lazy, slow, allocates).

namespace neotrace
{

// fills out with one code address per frame of the calling thread's stack, innermost
// frame first, after dropping `skip` caller frames; neotrace's own frames are never
// included, and return addresses are nudged back inside their call instruction so they
// symbolize to the right function and line. returns the number of addresses written,
// 0 when no capture backend exists for the platform.
size_t capture(std::span<void *> out, size_t skip = 0) noexcept;

// fixed-capacity capture result; a plain value type, fine to copy and store
struct trace
{
	static constexpr size_t max_frames{64};

	// trace of the caller's stack, with the call site itself as the first frame
	static trace current(size_t skip = 0) noexcept;

	[[nodiscard]] size_t size() const noexcept { return count; }
	[[nodiscard]] bool empty() const noexcept { return count == 0; }
	[[nodiscard]] void *const *begin() const noexcept { return frames; }
	[[nodiscard]] void *const *end() const noexcept { return frames + count; }
	[[nodiscard]] std::span<void *const> addresses() const noexcept;

	void *frames[max_frames]{};
	size_t count{0};
};

// everything symbolize() could learn about one address; fields stay empty/zero when unknown
struct frame_info
{
	void *address{nullptr};
	std::string symbol;  // demangled when possible
	std::string module;  // path of the executable or shared object containing the address
	uintptr_t offset{0}; // into symbol when symbol is known, otherwise into module
	std::string file;    // source file, only where the platform provides it
	uint32_t line{0};
};

frame_info symbolize(void *address) noexcept;

// demangles an Itanium C++ ABI name into a human-readable form: either a linkage name
// (_ZN3foo3barEv) or a bare type encoding as produced by typeid(...).name(). returns the
// input unchanged when it is not a mangled name, or when the platform has no demangler
// (e.g. msvc without cxxabi). this is the demangler symbolize() applies to frame symbols,
// exposed for callers that hold a mangled name from another source.
std::string demangle(const char *mangled) noexcept;

// "0x0000000100a3c044 in demo::leaf() + 0x18 (neotrace_demo) at main.cpp:12",
// with the pieces that could not be resolved left out
std::string to_string(const frame_info &frame) noexcept;

// symbolizes and formats every address, one "#N ..." line per frame, no trailing newline
std::string to_string(std::span<void *const> addresses) noexcept;
std::string to_string(const trace &t) noexcept;

} // namespace neotrace
