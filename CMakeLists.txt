cmake_minimum_required(VERSION 3.16)
project(Harbinger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(harbinger_formats
  src/formats/gim.cpp
  src/formats/sqh.cpp
)
target_include_directories(harbinger_formats PUBLIC src)

add_executable(sqhtool tools/sqhtool.cpp)
target_link_libraries(sqhtool PRIVATE harbinger_formats)

add_executable(gim2tga tools/gim2tga.cpp)
target_link_libraries(gim2tga PRIVATE harbinger_formats)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_sqh tests/test_sqh.cpp)
  target_link_libraries(test_sqh PRIVATE harbinger_formats)
  add_test(NAME sqh COMMAND test_sqh)

  add_executable(test_gim tests/test_gim.cpp)
  target_link_libraries(test_gim PRIVATE harbinger_formats)
  add_test(NAME gim COMMAND test_gim)
endif()
