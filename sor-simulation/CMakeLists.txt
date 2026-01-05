cmake_minimum_required(VERSION 3.10)
project(sor_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SIM_LIB_SRC
    src/director.cpp
    src/roles/patient_generator.cpp
    src/roles/patient.cpp
    src/roles/registration.cpp
    src/roles/triage.cpp
    src/roles/specialist.cpp
    src/visualization/visualizer.cpp
    src/visualization/log_parser.cpp
    src/visualization/state.cpp
    src/visualization/render_utils.cpp
    src/visualization/renderer.cpp
    src/ipc/message_queue.cpp
    src/ipc/shared_memory.cpp
    src/ipc/semaphore.cpp
    src/ipc/signals.cpp
    src/logging/logger.cpp
    src/util/error.cpp
    src/util/random.cpp
)

add_library(sor_sim_core STATIC ${SIM_LIB_SRC})
target_link_libraries(sor_sim_core PRIVATE pthread)
target_include_directories(sor_sim_core PUBLIC include)

add_executable(sor_sim src/main.cpp)
target_link_libraries(sor_sim PRIVATE sor_sim_core)
target_include_directories(sor_sim PRIVATE include)

enable_testing()
add_subdirectory(tests)
