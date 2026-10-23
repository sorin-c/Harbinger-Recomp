cmake_minimum_required(VERSION 3.16)
project(Harbinger LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HARBINGER_BUILD_APPS "Build the SDL3 applications" ON)
option(HARBINGER_FETCH_SDL "Download and build SDL3 when it is not installed" ON)

set(HARBINGER_WARNINGS $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall -Wextra -Wpedantic>)

# File formats and engine core: no platform dependencies, used by tests and tools.
add_library(harbinger_formats
  src/formats/acr.cpp
  src/formats/fnt.cpp
  src/formats/gim.cpp
  src/formats/itf.cpp
  src/formats/itm.cpp
  src/formats/mgr.cpp
  src/formats/sqh.cpp
)
target_include_directories(harbinger_formats PUBLIC src)
target_compile_options(harbinger_formats PRIVATE ${HARBINGER_WARNINGS})

add_library(harbinger_core
  src/engine/vfs.cpp
  src/render/sprite.cpp
  src/ui/item_animation.cpp
  src/ui/text.cpp
)
target_link_libraries(harbinger_core PUBLIC harbinger_formats)
target_compile_options(harbinger_core PRIVATE ${HARBINGER_WARNINGS})

add_executable(sqhtool tools/sqhtool.cpp)
target_link_libraries(sqhtool PRIVATE harbinger_formats)

add_executable(gim2tga tools/gim2tga.cpp)
target_link_libraries(gim2tga PRIVATE harbinger_formats)

if(HARBINGER_BUILD_APPS)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    if(NOT HARBINGER_FETCH_SDL)
      message(FATAL_ERROR "SDL3 not found; install it or enable HARBINGER_FETCH_SDL")
    endif()
    include(FetchContent)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.24/SDL3-3.2.24.tar.gz
    )
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
  endif()

  add_executable(harbinger src/app/harbinger.cpp)
  target_link_libraries(harbinger PRIVATE harbinger_core SDL3::SDL3)
  target_compile_options(harbinger PRIVATE ${HARBINGER_WARNINGS})

  add_executable(harbinger-viewer src/app/viewer.cpp)
  target_link_libraries(harbinger-viewer PRIVATE harbinger_core SDL3::SDL3)
  target_compile_options(harbinger-viewer PRIVATE ${HARBINGER_WARNINGS})
endif()

include(CTest)
if(BUILD_TESTING)
  foreach(t sqh gim core ui resources)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE harbinger_core)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()
