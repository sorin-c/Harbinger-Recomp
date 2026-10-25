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
  src/formats/animgroup.cpp
  src/formats/brs.cpp
  src/formats/chk.cpp
  src/formats/giz.cpp
  src/formats/agt.cpp
  src/formats/anm.cpp
  src/formats/gsc.cpp
  src/formats/gig.cpp
  src/formats/fnt.cpp
  src/formats/gim.cpp
  src/formats/itf.cpp
  src/formats/itm.cpp
  src/formats/emt.cpp
  src/formats/wpn.cpp
  src/formats/mgr.cpp
  src/formats/sqh.cpp
)
target_include_directories(harbinger_formats PUBLIC src)
target_compile_options(harbinger_formats PRIVATE ${HARBINGER_WARNINGS})

# dr_mp3 (public domain / MIT-0), a single header, pinned by commit and hash.
set(HARBINGER_DR_MP3 ${CMAKE_BINARY_DIR}/_deps/dr_libs/dr_mp3.h)
if(NOT EXISTS ${HARBINGER_DR_MP3})
  file(DOWNLOAD
    https://raw.githubusercontent.com/mackron/dr_libs/8159355353a9baeea04416df9626ac2b9afbf97b/dr_mp3.h
    ${HARBINGER_DR_MP3}
    EXPECTED_HASH SHA256=91f264e8559cc8ccecbcfc38abc77fff66533f5e95f666a4ba48238e69e190a4
    STATUS HARBINGER_DR_MP3_STATUS)
  list(GET HARBINGER_DR_MP3_STATUS 0 HARBINGER_DR_MP3_CODE)
  if(NOT HARBINGER_DR_MP3_CODE EQUAL 0)
    file(REMOVE ${HARBINGER_DR_MP3})
    message(FATAL_ERROR "Could not download dr_mp3.h: ${HARBINGER_DR_MP3_STATUS}")
  endif()
endif()

add_library(harbinger_core
  src/audio/mp3.cpp
  src/audio/wav.cpp
  src/engine/display.cpp
  src/engine/ini.cpp
  src/engine/settings.cpp
  src/engine/vfs.cpp
  src/render/sprite.cpp
  src/ui/controls.cpp
  src/ui/item_animation.cpp
  src/ui/text.cpp
  src/world/level.cpp
  src/world/gizmo_state.cpp
  src/world/motion.cpp
  src/world/player.cpp
  src/world/stats.cpp
  src/world/sprite_cache.cpp
  src/world/combat.cpp
  src/world/foes.cpp
  src/world/rooms.cpp
  src/world/actor.cpp
  src/world/bolts.cpp
  src/world/walkmap.cpp
  src/world/script.cpp
  src/world/loot.cpp
  src/world/inventory.cpp
  src/world/inventory_layout.cpp
  src/world/item_catalog.cpp
  src/world/item_text.cpp
  src/world/spawners.cpp
  src/world/portal.cpp
  src/world/umbilical.cpp
)
target_link_libraries(harbinger_core PUBLIC harbinger_formats)
target_include_directories(harbinger_core SYSTEM PRIVATE ${CMAKE_BINARY_DIR}/_deps/dr_libs)
target_compile_options(harbinger_core PRIVATE ${HARBINGER_WARNINGS})

add_executable(sqhtool tools/sqhtool.cpp)
target_link_libraries(sqhtool PRIVATE harbinger_formats)

add_executable(gim2tga tools/gim2tga.cpp)
target_link_libraries(gim2tga PRIVATE harbinger_formats)

add_executable(levelshot tools/levelshot.cpp)
target_link_libraries(levelshot PRIVATE harbinger_core)
target_compile_options(levelshot PRIVATE ${HARBINGER_WARNINGS})

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

  add_executable(harbinger src/app/harbinger.cpp src/app/assets.cpp src/app/screen.cpp src/app/sound.cpp
    src/app/world_view.cpp src/app/cutscene.cpp src/app/hud.cpp src/app/level_select.cpp)
  target_link_libraries(harbinger PRIVATE harbinger_core SDL3::SDL3)
  target_compile_options(harbinger PRIVATE ${HARBINGER_WARNINGS})
  if(WIN32)
    # Version information and an asInvoker manifest, so Windows sees a described, non-elevating app.
    target_sources(harbinger PRIVATE src/app/win/harbinger.rc)
    set_property(TARGET harbinger APPEND PROPERTY LINK_OPTIONS $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:NO>)
  endif()

  add_executable(harbinger-viewer src/app/viewer.cpp)
  target_link_libraries(harbinger-viewer PRIVATE harbinger_core SDL3::SDL3)
  target_compile_options(harbinger-viewer PRIVATE ${HARBINGER_WARNINGS})
endif()

include(CTest)
if(BUILD_TESTING)
  foreach(t sqh gim core ui resources audio world movement combat inventory stats)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE harbinger_core)
    add_test(NAME ${t} COMMAND test_${t})
  endforeach()
endif()
