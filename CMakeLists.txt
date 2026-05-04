cmake_minimum_required(VERSION 3.16)
project(os_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(os_sim
  OS_proj/src/main.c
  OS_proj/src/memory.c
  OS_proj/src/pcb.c
  OS_proj/src/queue.c
  OS_proj/src/scheduler.c
  OS_proj/src/mutex.c
  OS_proj/src/interpreter.c
  OS_proj/src/trace.c
)

target_include_directories(os_sim PRIVATE OS_proj/include)

if (MSVC)
  target_compile_options(os_sim PRIVATE /W4)
else()
  target_compile_options(os_sim PRIVATE -Wall -Wextra)
endif()
