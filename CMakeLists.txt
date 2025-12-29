cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0167 NEW)

project(LanDrop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(r src/receiver.cpp)
add_executable(s src/sender.cpp)

# Boost via vcpkg (config package)
find_package(Boost CONFIG REQUIRED)

if(TARGET Boost::headers)
  target_link_libraries(r PRIVATE Boost::headers)
  target_link_libraries(s PRIVATE Boost::headers)
elseif(TARGET Boost::boost)
  target_link_libraries(r PRIVATE Boost::boost)
  target_link_libraries(s PRIVATE Boost::boost)
else()
  target_include_directories(r PRIVATE ${Boost_INCLUDE_DIRS})
  target_include_directories(s PRIVATE ${Boost_INCLUDE_DIRS})
endif()

# Windows API target for Boost.Asio / networking
if(WIN32)
  target_compile_definitions(r PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(s PRIVATE _WIN32_WINNT=0x0A00)
endif()

