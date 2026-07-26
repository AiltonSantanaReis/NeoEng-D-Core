add_library(neoeng_dcore_distributed_reference STATIC
  src/distributed_reference.cpp
  src/dcore_replica_adapter.cpp)
add_library(NeoEng::DCoreDistributedReference ALIAS neoeng_dcore_distributed_reference)
set_target_properties(neoeng_dcore_distributed_reference PROPERTIES
  EXPORT_NAME DCoreDistributedReference
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(neoeng_dcore_distributed_reference PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(neoeng_dcore_distributed_reference PUBLIC neoeng_dcore)
if(WIN32)
  target_link_libraries(neoeng_dcore_distributed_reference PRIVATE ws2_32)
endif()
neoeng_set_warnings(neoeng_dcore_distributed_reference)
if(NEOENG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  target_compile_options(neoeng_dcore_distributed_reference PUBLIC
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(neoeng_dcore_distributed_reference PUBLIC
    -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(BUILD_TESTING)
  add_executable(neoeng_distributed_reference_tests
    tests/distributed_reference_tests.cpp)
  target_link_libraries(neoeng_distributed_reference_tests PRIVATE
    neoeng_dcore_distributed_reference)
  neoeng_set_warnings(neoeng_distributed_reference_tests)
  add_test(NAME neoeng_distributed_reference_tests
    COMMAND neoeng_distributed_reference_tests)
  set_tests_properties(neoeng_distributed_reference_tests PROPERTIES
    LABELS "smoke;dcore;distributed-reference;cs010")

  add_executable(neoeng_distributed_reference_probe
    apps/distributed_reference_probe.cpp)
  target_link_libraries(neoeng_distributed_reference_probe PRIVATE
    neoeng_dcore_distributed_reference)
  neoeng_set_warnings(neoeng_distributed_reference_probe)
  add_test(NAME neoeng_distributed_reference_probe
    COMMAND neoeng_distributed_reference_probe)
  set_tests_properties(neoeng_distributed_reference_probe PROPERTIES
    LABELS "full;dcore;distributed-reference;cs010")
endif()
