cmake_minimum_required(VERSION 3.25)
project(pip_brain LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PIP_BRAIN_BUILD_TESTS "Build the brain's doctest suite" ON)
set(TINY_AGENT_DIR "" CACHE PATH "Local tiny_agent checkout (header-only). Empty: fetch the pinned commit.")
set(PIP_RETE_DIR "" CACHE PATH "Local rete_cpp checkout (header-only). Empty: fetch the pinned commit.")

include(FetchContent)

# tiny_agent: vendored via add_subdirectory, examples/tests/bench off. It finds
# nlohmann_json, httplib, doctest through the vcpkg toolchain we were configured with.
set(TINY_AGENT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINY_AGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINY_AGENT_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(TINY_AGENT_FETCH_RETE OFF CACHE BOOL "" FORCE)
if(TINY_AGENT_DIR)
  add_subdirectory(${TINY_AGENT_DIR} tiny_agent EXCLUDE_FROM_ALL SYSTEM)
else()
  FetchContent_Declare(tiny_agent
    GIT_REPOSITORY https://github.com/tinyagent-cc/tiny_agent.git
    GIT_TAG aee29c7  # main, pinned 2026-08-23
    SYSTEM)
  FetchContent_MakeAvailable(tiny_agent)
endif()

# rete_cpp: headers only. tiny_agent's reflex.hpp includes <rete/rete.hpp>; the
# include dir lives on our targets, so tiny_agent itself needs no rete setting.
if(PIP_RETE_DIR)
  set(PIP_RETE_INCLUDE "${PIP_RETE_DIR}/include")
else()
  FetchContent_Declare(rete_cpp
    GIT_REPOSITORY https://github.com/tinyagent-cc/rete_cpp.git
    GIT_TAG 4c2e6c3a94b6759bc710c26d8d601eecb3cec2e3
    SOURCE_SUBDIR cmake-none)
  FetchContent_MakeAvailable(rete_cpp)
  set(PIP_RETE_INCLUDE "${rete_cpp_SOURCE_DIR}/include")
endif()

find_package(Threads REQUIRED)

add_library(brain_core STATIC
  src/body.cpp src/reflex.cpp src/judgment.cpp src/brain.cpp)
target_include_directories(brain_core PUBLIC src)
target_include_directories(brain_core SYSTEM PUBLIC ${PIP_RETE_INCLUDE})
target_link_libraries(brain_core PUBLIC tiny_agent Threads::Threads)
target_compile_options(brain_core PUBLIC -Wall -Wextra)

add_executable(pip-brain src/main.cpp)
target_link_libraries(pip-brain PRIVATE brain_core)

if(PIP_BRAIN_BUILD_TESTS)
  enable_testing()
  find_package(doctest CONFIG REQUIRED)
  foreach(t policy wire body reflex guardrail judgment brain)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${t}.cpp)
      add_executable(test_${t} tests/test_${t}.cpp)
      target_link_libraries(test_${t} PRIVATE brain_core doctest::doctest)
      add_test(NAME ${t} COMMAND test_${t})
    endif()
  endforeach()
endif()
