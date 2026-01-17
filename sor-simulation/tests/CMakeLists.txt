enable_testing()

add_library(test_support STATIC
    sim_runner.cpp
    log_utils.cpp
)

target_include_directories(test_support
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_support PRIVATE sor_sim_core pthread)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(test_support PRIVATE stdc++fs)
endif()

add_executable(test_waiting_room_capacity test_waiting_room.cpp)
target_link_libraries(test_waiting_room_capacity PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME waiting_room_capacity
    COMMAND test_waiting_room_capacity $<TARGET_FILE:sor_sim>
)

add_executable(test_reg2_spawn test_reg2_spawn.cpp)
target_link_libraries(test_reg2_spawn PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME reg2_spawn
    COMMAND test_reg2_spawn $<TARGET_FILE:sor_sim>
)

add_executable(test_summary_invariants test_summary_invariants.cpp)
target_link_libraries(test_summary_invariants PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME summary_invariants
    COMMAND test_summary_invariants $<TARGET_FILE:sor_sim>
)

add_executable(test_sigusr2_shutdown test_sigusr2_shutdown.cpp)
target_link_libraries(test_sigusr2_shutdown PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME sigusr2_shutdown
    COMMAND test_sigusr2_shutdown $<TARGET_FILE:sor_sim>
)

add_executable(test_vip_guardian test_vip_guardian.cpp)
target_link_libraries(test_vip_guardian PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME vip_guardian
    COMMAND test_vip_guardian $<TARGET_FILE:sor_sim>
)

add_executable(test_triage_distribution test_triage_distribution.cpp)
target_link_libraries(test_triage_distribution PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME triage_distribution
    COMMAND test_triage_distribution $<TARGET_FILE:sor_sim>
)

add_executable(test_outcome_distribution test_outcome_distribution.cpp)
target_link_libraries(test_outcome_distribution PRIVATE test_support sor_sim_core pthread)

add_test(
    NAME outcome_distribution
    COMMAND test_outcome_distribution $<TARGET_FILE:sor_sim>
)
