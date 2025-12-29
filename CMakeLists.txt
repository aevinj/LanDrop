cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0167 NEW)

project(LanDrop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(r src/receiver.cpp)
add_executable(s src/sender.cpp)

if(WIN32)
  target_compile_definitions(r PRIVATE _WIN32_WINNT=0x0A00)
  target_compile_definitions(s PRIVATE _WIN32_WINNT=0x0A00)

  # Windows (vcpkg sub-ports)
  find_package(boost_asio CONFIG REQUIRED)
  find_package(boost_system CONFIG REQUIRED)
  target_link_libraries(r PRIVATE Boost::asio Boost::system)
  target_link_libraries(s PRIVATE Boost::asio Boost::system)

else()
  # macOS/Linux (Homebrew/system Boost)
  find_package(Boost CONFIG REQUIRED)

  if(TARGET Boost::headers)
    target_link_libraries(r PRIVATE Boost::headers)
    target_link_libraries(s PRIVATE Boost::headers)
  elseif(TARGET Boost::boost)
    target_link_libraries(r PRIVATE Boost::boost)
    target_link_libraries(s PRIVATE Boost::boost)
  elseif(DEFINED Boost_INCLUDE_DIRS)
    target_include_directories(r PRIVATE ${Boost_INCLUDE_DIRS})
    target_include_directories(s PRIVATE ${Boost_INCLUDE_DIRS})
  else()
    message(FATAL_ERROR "Boost found but no usable include target/dirs were provided.")
  endif()

  # Do NOT require libboost_system on macOS/Homebrew:
  # Boost.System is header-only in modern Boost; the stub library may be absent. :contentReference[oaicite:3]{index=3}
endif()

