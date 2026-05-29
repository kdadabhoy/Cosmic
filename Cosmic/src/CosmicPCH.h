#pragma once

// Standard Library — included by most engine translation units
#include <string>
#include <sstream>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>

// Windows — large header; parsing it once here saves it on every TU that needs it
#ifdef _WIN32
#include <windows.h>
#endif

// GLM — pulled in by camera, renderer, scene, and most game-facing code
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// spdlog — pulled in transitively by Log.h across almost every cpp
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
