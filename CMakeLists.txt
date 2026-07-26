cmake_minimum_required(VERSION 3.25)
project(NeoEngDCore VERSION 1.9.0 LANGUAGES C CXX)

option(NEOENG_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NEOENG_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(NEOENG_DCORE_BUILD_FULL_TOOLSET "Build all portable Year-1 historical tools" ON)
option(NEOENG_DCORE_BUILD_RESEARCH_TOOLS "Build Year-1 v0.28 gate/research tools" ON)
option(NEOENG_DCORE_BUILD_VIEW_LAB "Build the optional read-only visual diagnostics companion" OFF)
option(NEOENG_DCORE_BUILD_HOST_SDK "Build the stable C host integration boundary" ON)
option(NEOENG_DCORE_ENABLE_INSTALL "Enable SDK installation and CMake package export" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options(/EHsc /Zc:__cplusplus)
endif()
include(cmake/NeoEngWarnings.cmake)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Boost 1.80 CONFIG REQUIRED)

if(WIN32 AND MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-resource-dir
    OUTPUT_VARIABLE NEOENG_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE NEOENG_CLANG_RESOURCE_RESULT)
  if(NOT NEOENG_CLANG_RESOURCE_RESULT EQUAL 0 OR NEOENG_CLANG_RESOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "Unable to query clang resource directory for compiler-rt builtins")
  endif()
  file(TO_CMAKE_PATH "${NEOENG_CLANG_RESOURCE_DIR}" NEOENG_CLANG_RESOURCE_DIR)
  set(NEOENG_CLANG_BUILTINS_LIBRARY "${NEOENG_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.builtins-x86_64.lib")
  if(NOT EXISTS "${NEOENG_CLANG_BUILTINS_LIBRARY}")
    message(FATAL_ERROR "Missing clang-cl compiler-rt builtins: ${NEOENG_CLANG_BUILTINS_LIBRARY}")
  endif()
  add_library(neoeng_clang_rt_builtins STATIC IMPORTED GLOBAL)
  set_target_properties(neoeng_clang_rt_builtins PROPERTIES IMPORTED_LOCATION "${NEOENG_CLANG_BUILTINS_LIBRARY}")
endif()

set(NEOENG_GNU_LD_WRAP_SUPPORTED OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(NEOENG_GNU_LD_WRAP_SUPPORTED ON)
endif()
set(NEOENG_DCORE_CORE_SOURCES
  src/hash.cpp
  src/crypto_hash.cpp
  src/state_evidence.cpp
  src/diagnostics.cpp
  src/support_bundle.cpp
  src/simulation.cpp
  src/rollback.cpp
  src/snapshot_store.cpp
  src/immutable_world.cpp
  src/immutable_rollback.cpp
  src/persistent_checkpoint.cpp
  src/offline_oracle.cpp
  src/active_world.cpp
  src/component_world.cpp
  src/radix_world.cpp
  src/contextual_policy.cpp
  src/fixed_simd.cpp
  src/fixed_slot_allocator.cpp
  src/uncertainty_lab.cpp
  src/fixed_raa_microkernel.cpp
  src/fixed_raa_active_island_shadow.cpp
  src/broadphase.cpp
  src/epoch_arena.cpp
  src/contact_solver.cpp
  src/chain_solver.cpp
  src/temporal_contact.cpp
  src/interactive_rollback.cpp
  src/island_runtime.cpp
  src/advanced_island_solver.cpp
  src/general_lcp_solver.cpp
  src/weighted_contact_projection.cpp
  src/arbitrary_normal_projection.cpp
  src/axis_forest_projection.cpp
  src/atomic_physics.cpp
  src/weighted_tree_projection.cpp
  src/atomic_temporal_physics.cpp
  src/paged_atomic_history.cpp
  src/paged_atomic_temporal_physics.cpp
  src/island_pair_cache.cpp
  src/small_oblique_grid_oracle.cpp
  src/versioned_island_pair_history.cpp
  src/oblique_tree_grid_dp.cpp
  src/dynamic_island_pair_history.cpp
  src/authoritative_paged_temporal_physics.cpp
  src/exact_oblique_tree_oracle.cpp
  src/oblique_star_projection.cpp
  src/segmented_dynamic_pair_history.cpp
  src/paged_segmented_pair_history.cpp
  src/segmented_authoritative_paged_temporal_physics.cpp
  src/network_security.cpp
  src/session_security.cpp
  src/observability.cpp
  src/recovery.cpp
  src/recovery_contract.cpp
  src/hardware_profile.cpp
  src/operational_runtime.cpp
  src/visual_correlation.cpp
)
add_library(neoeng_dcore ${NEOENG_DCORE_CORE_SOURCES})
add_library(NeoEng::DCore ALIAS neoeng_dcore)
set_target_properties(neoeng_dcore PROPERTIES EXPORT_NAME DCore POSITION_INDEPENDENT_CODE ON)
target_include_directories(neoeng_dcore PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
if(TARGET Boost::headers)
  target_link_libraries(neoeng_dcore PUBLIC Boost::headers)
elseif(TARGET Boost::boost)
  target_link_libraries(neoeng_dcore PUBLIC Boost::boost)
else()
  target_include_directories(neoeng_dcore PUBLIC ${Boost_INCLUDE_DIRS})
endif()
if(TARGET neoeng_clang_rt_builtins)
  target_link_libraries(neoeng_dcore PUBLIC neoeng_clang_rt_builtins)
endif()
neoeng_set_warnings(neoeng_dcore)
if(NEOENG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  target_compile_options(neoeng_dcore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(neoeng_dcore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

function(neoeng_dcore_add_tool target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE neoeng_dcore)
  neoeng_set_warnings(${target})
endfunction()
function(neoeng_dcore_add_gnu_wrap_benchmark target source)
  if(NOT NEOENG_GNU_LD_WRAP_SUPPORTED)
    message(STATUS "${target}: source retained; executable disabled because GNU ld --wrap is unavailable")
    return()
  endif()
  neoeng_dcore_add_tool(${target} ${source})
  target_link_options(${target} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
endfunction()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_dcore_lab apps/core_lab_main.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_determinism_probe apps/determinism_probe.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_network_packet_fuzz apps/network_packet_fuzz.cpp)
  neoeng_dcore_add_tool(neoeng_session_security_probe apps/session_security_probe.cpp)
  neoeng_dcore_add_tool(neoeng_session_handshake_fuzz apps/session_handshake_fuzz.cpp)
  neoeng_dcore_add_tool(neoeng_time_travel_cli apps/dcore_time_travel_cli.cpp)
  neoeng_dcore_add_tool(neoeng_recovery_fault_injection apps/recovery_fault_injection.cpp)
  neoeng_dcore_add_tool(neoeng_recovery_contract_probe apps/recovery_contract_probe.cpp)
  neoeng_dcore_add_tool(neoeng_hardware_profile_probe apps/hardware_profile_probe.cpp)
  neoeng_dcore_add_tool(neoeng_ecs_maintenance_benchmark apps/ecs_maintenance_benchmark.cpp)
  neoeng_dcore_add_tool(neoeng_state_evidence_probe apps/state_evidence_probe.cpp)
  neoeng_dcore_add_tool(neoeng_state_evidence_fuzz apps/state_evidence_fuzz.cpp)
  neoeng_dcore_add_tool(neoeng_support_bundle_probe apps/support_bundle_probe.cpp)
  neoeng_dcore_add_tool(neoeng_support_bundle_fuzz apps/support_bundle_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_benchmark apps/benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_snapshot_benchmark apps/snapshot_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_adaptive_benchmark apps/adaptive_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_snapshot_fuzz apps/snapshot_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_rollback_matrix apps/rollback_matrix_v03.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v04_benchmark apps/v04_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_immutable_fuzz apps/immutable_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v05_benchmark apps/v05_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_active_fuzz apps/active_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v06_benchmark apps/v06_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v06_fuzz apps/v06_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v07_benchmark apps/v07_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v07_fuzz apps/v07_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v08_benchmark apps/v08_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v08_fuzz apps/v08_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v09_benchmark apps/v09_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v09_fuzz apps/v09_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v10_benchmark apps/v10_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v10_fuzz apps/v10_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v11_benchmark apps/v11_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v11_fuzz apps/v11_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v12_benchmark apps/v12_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v12_fuzz apps/v12_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v12_policy_benchmark apps/v12_policy_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v13_benchmark apps/v13_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v13_fuzz apps/v13_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v14_benchmark apps/v14_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v14_fuzz apps/v14_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v15_benchmark apps/v15_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v15_fuzz apps/v15_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v17_fuzz apps/v17_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v18_fuzz apps/v18_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v19_fuzz apps/v19_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v20_fuzz apps/v20_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v21_fuzz apps/v21_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v22_benchmark apps/v22_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v22_fuzz apps/v22_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v23_fuzz apps/v23_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v24_fuzz apps/v24_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v25_fuzz apps/v25_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v26_benchmark apps/v26_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v26_fuzz apps/v26_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v27_benchmark apps/v27_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v27_fuzz apps/v27_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_raa_allocation_probe apps/v28_raa_allocation_probe.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_bare_metal_rollback apps/v28_bare_metal_rollback.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_raa_selective_benchmark apps/v28_raa_selective_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_raa_selective_fuzz apps/v28_raa_selective_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_interval_arithmetic_audit apps/v28_interval_arithmetic_audit.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_v28_active_island_shadow apps/v28_active_island_shadow.cpp)
endif()
if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  neoeng_dcore_add_tool(neoeng_dcore_preclosure apps/v28_year1_preclosure.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_tool(neoeng_v16_fuzz apps/v16_fuzz.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v16_benchmark apps/v16_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v17_benchmark apps/v17_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v18_benchmark apps/v18_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v19_benchmark apps/v19_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v20_benchmark apps/v20_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v21_benchmark apps/v21_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v23_benchmark apps/v23_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v24_benchmark apps/v24_benchmark.cpp)
endif()
if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
  neoeng_dcore_add_gnu_wrap_benchmark(neoeng_v25_benchmark apps/v25_benchmark.cpp)
endif()

if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  add_executable(neoeng_v28_raa_sanitizer_probe apps/v28_raa_sanitizer_probe.cpp src/fixed_raa_microkernel.cpp)
  target_include_directories(neoeng_v28_raa_sanitizer_probe PRIVATE include)
  neoeng_set_warnings(neoeng_v28_raa_sanitizer_probe)
  add_executable(neoeng_v28_raa_decomposition_benchmark apps/v28_raa_decomposition_benchmark.cpp src/fixed_raa_decomposition.cpp)
  target_link_libraries(neoeng_v28_raa_decomposition_benchmark PRIVATE neoeng_dcore)
  target_include_directories(neoeng_v28_raa_decomposition_benchmark PRIVATE include)
  neoeng_set_warnings(neoeng_v28_raa_decomposition_benchmark)
  add_executable(neoeng_v28_raa_selective_sanitizer apps/v28_raa_selective_fuzz.cpp src/fixed_raa_microkernel.cpp)
  target_include_directories(neoeng_v28_raa_selective_sanitizer PRIVATE include)
  neoeng_set_warnings(neoeng_v28_raa_selective_sanitizer)
  if(NEOENG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    foreach(target neoeng_v28_raa_sanitizer_probe neoeng_v28_raa_selective_sanitizer)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
  endif()
endif()

if(MSVC AND NEOENG_DCORE_BUILD_FULL_TOOLSET)
  foreach(target neoeng_immutable_fuzz neoeng_active_fuzz)
    if(TARGET ${target})
      target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
  endforeach()
endif()
if(TARGET neoeng_v11_benchmark AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(neoeng_v11_benchmark PRIVATE -Wno-mismatched-new-delete)
endif()

if(NEOENG_DCORE_BUILD_VIEW_LAB)
  add_subdirectory(modules/view_lab)
endif()
if(NEOENG_DCORE_BUILD_HOST_SDK)
  add_subdirectory(modules/host_sdk)
endif()

if(NEOENG_DCORE_ENABLE_INSTALL)
  set(NEOENG_DCORE_INSTALL_TARGETS neoeng_dcore)
  if(TARGET neoeng_dcore_host_sdk)
    list(APPEND NEOENG_DCORE_INSTALL_TARGETS neoeng_dcore_host_sdk)
  endif()
  install(TARGETS ${NEOENG_DCORE_INSTALL_TARGETS}
    EXPORT NeoEngDCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY include/neoeng/core DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/neoeng)
  if(TARGET neoeng_dcore_host_sdk)
    install(FILES modules/host_sdk/include/neoeng/dcore_host.h
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/neoeng)
  endif()
  configure_package_config_file(
    cmake/NeoEngDCoreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NeoEngDCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeoEngDCore)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NeoEngDCoreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(EXPORT NeoEngDCoreTargets
    FILE NeoEngDCoreTargets.cmake
    NAMESPACE NeoEng::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeoEngDCore)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NeoEngDCoreConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NeoEngDCoreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NeoEngDCore)
endif()
if(NEOENG_GNU_LD_WRAP_SUPPORTED AND NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
  foreach(target neoeng_v28_raa_allocation_probe neoeng_v28_bare_metal_rollback)
    if(TARGET ${target})
      target_compile_definitions(${target} PRIVATE NEOENG_C_ALLOCATION_PROBE_SUPPORTED=1)
      target_link_options(${target} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign)
    endif()
  endforeach()
endif()

if(BUILD_TESTING)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  add_test(NAME neoeng_product_contract_verify
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify_product_contract.py --check-report)
  add_test(NAME neoeng_product_contract_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify_product_contract.py --self-test)
  add_test(NAME neoeng_product_assurance_verify
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify_product_assurance.py --check-report)
  add_test(NAME neoeng_product_assurance_self_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/verify_product_assurance.py --self-test)
  set_tests_properties(
    neoeng_product_contract_verify neoeng_product_contract_self_test
    neoeng_product_assurance_verify neoeng_product_assurance_self_test
    PROPERTIES LABELS "governance;release-gate;dcore")

  add_executable(neoeng_tests tests/test_main.cpp)
  target_link_libraries(neoeng_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_tests)
  add_test(NAME neoeng_tests COMMAND neoeng_tests)
  add_executable(neoeng_operational_hardening_tests tests/operational_hardening_tests.cpp)
  target_link_libraries(neoeng_operational_hardening_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_operational_hardening_tests)
  add_test(NAME neoeng_operational_hardening_tests COMMAND neoeng_operational_hardening_tests)
  add_executable(neoeng_session_recovery_contract_tests tests/session_recovery_contract_tests.cpp)
  target_link_libraries(neoeng_session_recovery_contract_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_session_recovery_contract_tests)
  add_test(NAME neoeng_session_recovery_contract_tests COMMAND neoeng_session_recovery_contract_tests)
  add_executable(neoeng_observability_support_tests tests/observability_support_tests.cpp)
  target_link_libraries(neoeng_observability_support_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_observability_support_tests)
  add_test(NAME neoeng_observability_support_tests COMMAND neoeng_observability_support_tests)
  add_executable(neoeng_state_evidence_tests tests/state_evidence_tests.cpp)
  target_link_libraries(neoeng_state_evidence_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_state_evidence_tests)
  add_test(NAME neoeng_state_evidence_tests COMMAND neoeng_state_evidence_tests)
  add_executable(neoeng_hardware_qualification_tests tests/hardware_qualification_tests.cpp)
  target_link_libraries(neoeng_hardware_qualification_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_hardware_qualification_tests)
  add_test(NAME neoeng_hardware_qualification_tests COMMAND neoeng_hardware_qualification_tests)
  set_tests_properties(neoeng_tests neoeng_operational_hardening_tests
    neoeng_session_recovery_contract_tests neoeng_state_evidence_tests
    neoeng_observability_support_tests neoeng_hardware_qualification_tests
    PROPERTIES LABELS "smoke;dcore")

  if(NEOENG_DCORE_BUILD_FULL_TOOLSET)
    add_test(NAME neoeng_snapshot_fuzz COMMAND neoeng_snapshot_fuzz)
    add_test(NAME neoeng_network_packet_fuzz_smoke COMMAND neoeng_network_packet_fuzz 25000)
    add_test(NAME neoeng_session_security_probe COMMAND neoeng_session_security_probe)
    add_test(NAME neoeng_session_handshake_fuzz_smoke COMMAND neoeng_session_handshake_fuzz 25000)
    add_test(NAME neoeng_recovery_fault_injection COMMAND neoeng_recovery_fault_injection)
    add_test(NAME neoeng_recovery_contract_probe COMMAND neoeng_recovery_contract_probe)
    add_test(NAME neoeng_state_evidence_probe COMMAND neoeng_state_evidence_probe)
    add_test(NAME neoeng_state_evidence_fuzz_smoke COMMAND neoeng_state_evidence_fuzz 25000)
    add_test(NAME neoeng_support_bundle_probe COMMAND neoeng_support_bundle_probe ${CMAKE_BINARY_DIR}/support_bundle_probe)
    add_test(NAME neoeng_support_bundle_fuzz_smoke COMMAND neoeng_support_bundle_fuzz 25000)
    add_test(NAME neoeng_ecs_maintenance_benchmark_smoke
      COMMAND neoeng_ecs_maintenance_benchmark ${CMAKE_BINARY_DIR}/ecs_maintenance_smoke 256 16 32 4)
    add_test(NAME neoeng_ecs_scope_evidence_verifier_smoke
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/qualification/verify_ecs_scope_evidence.py
        ${CMAKE_BINARY_DIR}/ecs_maintenance_smoke --write-report)
    add_test(NAME neoeng_ecs_scope_evidence_verifier_self_test
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/qualification/verify_ecs_scope_evidence.py --self-test)
    set_tests_properties(neoeng_ecs_scope_evidence_verifier_smoke
      PROPERTIES DEPENDS neoeng_ecs_maintenance_benchmark_smoke LABELS "full;dcore;ecs-scope")
    set_tests_properties(neoeng_ecs_scope_evidence_verifier_self_test
      PROPERTIES LABELS "smoke;dcore;ecs-scope")
    add_test(NAME neoeng_immutable_fuzz COMMAND neoeng_immutable_fuzz)
    add_test(NAME neoeng_active_fuzz COMMAND neoeng_active_fuzz)
    add_test(NAME neoeng_v06_fuzz COMMAND neoeng_v06_fuzz 500)
    add_test(NAME neoeng_v07_fuzz COMMAND neoeng_v07_fuzz 500)
    add_test(NAME neoeng_v08_fuzz COMMAND neoeng_v08_fuzz 500)
    add_test(NAME neoeng_v09_fuzz COMMAND neoeng_v09_fuzz 500)
    add_test(NAME neoeng_v10_fuzz COMMAND neoeng_v10_fuzz 500)
    add_test(NAME neoeng_v11_fuzz COMMAND neoeng_v11_fuzz 500)
    add_test(NAME neoeng_v12_fuzz COMMAND neoeng_v12_fuzz 250)
    add_test(NAME neoeng_v13_fuzz COMMAND neoeng_v13_fuzz 250)
    add_test(NAME neoeng_v14_fuzz COMMAND neoeng_v14_fuzz 250)
    add_test(NAME neoeng_v15_fuzz COMMAND neoeng_v15_fuzz 250)
    add_test(NAME neoeng_v16_fuzz COMMAND neoeng_v16_fuzz 250)
    add_test(NAME neoeng_v17_fuzz COMMAND neoeng_v17_fuzz 250)
    add_test(NAME neoeng_v18_fuzz COMMAND neoeng_v18_fuzz 250)
    add_test(NAME neoeng_v19_fuzz COMMAND neoeng_v19_fuzz 50)
    add_test(NAME neoeng_v20_fuzz COMMAND neoeng_v20_fuzz 50)
    add_test(NAME neoeng_v21_fuzz COMMAND neoeng_v21_fuzz 100)
    add_test(NAME neoeng_v22_fuzz COMMAND neoeng_v22_fuzz 100)
    add_test(NAME neoeng_v23_fuzz COMMAND neoeng_v23_fuzz 100)
    add_test(NAME neoeng_v24_fuzz COMMAND neoeng_v24_fuzz 100)
    add_test(NAME neoeng_v25_fuzz COMMAND neoeng_v25_fuzz 100)
    add_test(NAME neoeng_v26_fuzz COMMAND neoeng_v26_fuzz 100)
    add_test(NAME neoeng_v27_fuzz COMMAND neoeng_v27_fuzz 100)
    set_tests_properties(
      neoeng_snapshot_fuzz neoeng_network_packet_fuzz_smoke
      neoeng_session_security_probe neoeng_session_handshake_fuzz_smoke
      neoeng_recovery_fault_injection neoeng_recovery_contract_probe
      neoeng_state_evidence_probe neoeng_state_evidence_fuzz_smoke
      neoeng_support_bundle_probe neoeng_support_bundle_fuzz_smoke
      neoeng_ecs_maintenance_benchmark_smoke
      neoeng_immutable_fuzz neoeng_active_fuzz
      neoeng_v06_fuzz
      neoeng_v07_fuzz
      neoeng_v08_fuzz
      neoeng_v09_fuzz
      neoeng_v10_fuzz
      neoeng_v11_fuzz
      neoeng_v12_fuzz
      neoeng_v13_fuzz
      neoeng_v14_fuzz
      neoeng_v15_fuzz
      neoeng_v16_fuzz
      neoeng_v17_fuzz
      neoeng_v18_fuzz
      neoeng_v19_fuzz
      neoeng_v20_fuzz
      neoeng_v21_fuzz
      neoeng_v22_fuzz
      neoeng_v23_fuzz
      neoeng_v24_fuzz
      neoeng_v25_fuzz
      neoeng_v26_fuzz
      neoeng_v27_fuzz
      PROPERTIES LABELS "full;dcore")
  endif()

  if(NEOENG_DCORE_BUILD_RESEARCH_TOOLS)
    add_test(NAME neoeng_v28_raa_allocation_probe COMMAND neoeng_v28_raa_allocation_probe 32 8)
    add_test(NAME neoeng_v28_raa_sanitizer_probe COMMAND neoeng_v28_raa_sanitizer_probe 32)
    add_test(NAME neoeng_v28_raa_decomposition_benchmark COMMAND neoeng_v28_raa_decomposition_benchmark ${CMAKE_BINARY_DIR}/v28_raa_decomposition_test 2 128)
    add_test(NAME neoeng_v28_raa_selective_benchmark COMMAND neoeng_v28_raa_selective_benchmark ${CMAKE_BINARY_DIR}/v28_raa_selective_test 128 2)
    add_test(NAME neoeng_v28_raa_selective_fuzz COMMAND neoeng_v28_raa_selective_fuzz ${CMAKE_BINARY_DIR}/v28_raa_selective_fuzz 2 64)
    add_test(NAME neoeng_v28_interval_arithmetic_audit COMMAND neoeng_v28_interval_arithmetic_audit 100000)
    add_test(NAME neoeng_dcore_replay_smoke COMMAND neoeng_dcore_preclosure replay ${CMAKE_BINARY_DIR}/dcore_replay_smoke 10000)
    add_test(NAME neoeng_dcore_history_smoke COMMAND neoeng_dcore_preclosure history ${CMAKE_BINARY_DIR}/dcore_history_smoke 900)
    add_test(NAME neoeng_dcore_network_smoke COMMAND neoeng_dcore_preclosure network ${CMAKE_BINARY_DIR}/dcore_network_smoke 2000)
    add_test(NAME neoeng_v28_active_island_shadow COMMAND neoeng_v28_active_island_shadow ${CMAKE_BINARY_DIR}/v28_active_island_shadow_test 2 3 8 0x2805000000000001)
    set_tests_properties(
      neoeng_dcore_replay_smoke neoeng_dcore_history_smoke neoeng_dcore_network_smoke
      PROPERTIES LABELS "smoke;dcore")
    set_tests_properties(
      neoeng_v28_raa_allocation_probe neoeng_v28_raa_sanitizer_probe
      neoeng_v28_raa_decomposition_benchmark neoeng_v28_raa_selective_benchmark
      neoeng_v28_raa_selective_fuzz neoeng_v28_interval_arithmetic_audit
      neoeng_v28_active_island_shadow PROPERTIES LABELS "full;dcore")
  endif()
endif()

if(BUILD_TESTING AND NEOENG_DCORE_BUILD_HOST_SDK AND NEOENG_DCORE_ENABLE_INSTALL)
  add_test(NAME neoeng_host_sdk_install_consumer
    COMMAND ${CMAKE_COMMAND}
      -DNEOENG_SOURCE_DIR=${CMAKE_SOURCE_DIR}
      -DNEOENG_BUILD_DIR=${CMAKE_BINARY_DIR}
      -DNEOENG_TEST_CONFIG=$<CONFIG>
      -DNEOENG_CMAKE_GENERATOR=${CMAKE_GENERATOR}
      -DNEOENG_C_COMPILER=${CMAKE_C_COMPILER}
      -DNEOENG_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -P ${CMAKE_SOURCE_DIR}/tests/cmake/run_host_sdk_install_consumer.cmake)
  set_tests_properties(neoeng_host_sdk_install_consumer
    PROPERTIES LABELS "smoke;dcore;host-sdk;install")
endif()
