# ----------------------------
# 1. Project Setup
# ----------------------------
project(Sandbox LANGUAGES C CXX)

# ----------------------------
# 2. Identify Source Files
# ----------------------------
file(GLOB_RECURSE SANDBOX_SOURCES "src/*.cpp")
file(GLOB_RECURSE SANDBOX_HEADERS "src/*.h")

add_executable(Sandbox ${SANDBOX_SOURCES} ${SANDBOX_HEADERS})

# ----------------------------
# 3. Include and Linking
# ----------------------------
target_include_directories(Sandbox PRIVATE 
    src
    src/projects
    src/utils
)

# Link to Cosmic - inherits all spdlog/glfw/glm headers and /utf-8 flags
target_link_libraries(Sandbox PRIVATE Cosmic)

# ----------------------------
# 4. Folder Organization (Visual Studio)
# ----------------------------
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SANDBOX_SOURCES} ${SANDBOX_HEADERS})

# ----------------------------
# 5. Asset Management
# ----------------------------

# Copy Engine-wide assets (Shaders, default textures)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../Cosmic/assets/" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets")

# Copy DinoProject specific assets
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/projects/DinoProject/assets/" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets/DinoProject")