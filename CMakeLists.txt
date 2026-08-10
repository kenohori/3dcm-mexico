cmake_minimum_required(VERSION 3.16)
project(elevadormx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# CGAL's config calls find_package(Boost) in a way that trips CMP0167 on CMake >= 4
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE TRUE)

find_package(CGAL REQUIRED COMPONENTS Core)
find_package(GDAL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(elevadormx
  elevadormx/elevadormx/main.cpp
)

target_include_directories(elevadormx PRIVATE elevadormx/elevadormx)
target_link_libraries(elevadormx PRIVATE CGAL::CGAL_Core GDAL::GDAL nlohmann_json::nlohmann_json)
