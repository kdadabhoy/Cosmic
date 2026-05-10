# ----------------------------
# 1. Project Setup
# ----------------------------
project(Sandbox LANGUAGES C CXX)

# ----------------------------
# 2. Identify Source Files
# ----------------------------
# We still use GLOB for the Sandbox, but we separate them into groups 
# This helps IDEs like Visual Studio show them in a nice folder tree.
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

# Link to our Engine
target_link_libraries(Sandbox PRIVATE Cosmic)

# ----------------------------
# 4. Folder Organization (Visual Studio / Xcode)
# ----------------------------
# This makes your files look organized in the Solution Explorer 
# instead of one giant list.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SANDBOX_SOURCES} ${SANDBOX_HEADERS})

# ----------------------------
# 5. Asset Management & Debugging
# ----------------------------

# 1. Copy Global Engine Assets
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../Cosmic/assets/" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets")

# 2. Copy Project-Specific Assets into unique subfolders
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/projects/DinoProject/assets/" 
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/assets/DinoProject")

# Repeat for other projects...