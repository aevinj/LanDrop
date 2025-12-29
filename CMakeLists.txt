cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0167 NEW)

project(LanDrop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(r src/receiver.cpp)
add_executable(s src/sender.cpp)

# ---- Boost.Asio via vcpkg -------------------------------------------------
# Use vcpkg manifest mode with:
#   -DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake
#
# vcpkg.json should include:
#   "boost-asio", "boost-system"
#
# Explicitly target Windows 10+ APIs to avoid Boost.Asio defaulting to Win7.
if(WIN32)
  target_compile_definitions(r PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(s PRIVATE _WIN32_WINNT=0x0A00)
endif()

if(DEFINED VCPKG_MANIFEST_MODE)
  # vcpkg sub-ports (Windows)
  find_package(boost_asio CONFIG REQUIRED)
  find_package(boost_system CONFIG REQUIRED)

  target_link_libraries(r PRIVATE Boost::asio Boost::system)
  target_link_libraries(s PRIVATE Boost::asio Boost::system)

else()
  # System / Homebrew Boost (macOS, Linux)
  find_package(Boost REQUIRED COMPONENTS system)

  if(TARGET Boost::headers)
    target_link_libraries(r PRIVATE Boost::headers Boost::system)
    target_link_libraries(s PRIVATE Boost::headers Boost::system)
  else()
    target_link_libraries(r PRIVATE Boost::boost Boost::system)
    target_link_libraries(s PRIVATE Boost::boost Boost::system)
  endif()
endif()
