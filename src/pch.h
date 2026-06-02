#pragma once

#include "config.h"

// local headers which transitively include a lot of other stuff but don't change often themselves
#include "BaseEnvironment.h"
#include "MakeDelegateWrapper.h"
#include "Hashing.h"
#include "MD5Hash.h"
#include "Color.h"
#include "Rect.h"  // mostly due to formatters
#include "ContainerRanges.h"

#include "SyncMutex.h"
#include "SyncCV.h"
#include "SyncJthread.h"
#include "SyncOnce.h"
#include "SyncStoptoken.h"

// glm, header only, template-heavy
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/mat2x2.hpp"
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/trigonometric.hpp"

// we use fmt pre-compiled but the headers are still pretty template-heavy
#ifndef BUILD_TOOLS_ONLY
#include "fmt/format.h"
#include "fmt/compile.h"
#include "fmt/ostream.h"
#include "fmt/chrono.h"
#include "fmt/ranges.h"
#include "fmt/printf.h"
#endif

// boost is insanely fat, even if we avoid including it transitively
#include "boost/sort/pdqsort/pdqsort.hpp"
#include "boost/sort/spinsort/spinsort.hpp"
#include "boost/sort/spreadsort/spreadsort.hpp"

// commonly-included stdlib includes
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// i have no idea where these come from but they seem to be transitively included through some stdlib header,
// and they're so ridiculously slow to compile that it's worth putting them here as well
#include <print>
#include <format>
