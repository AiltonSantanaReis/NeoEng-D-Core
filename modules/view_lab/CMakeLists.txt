add_library(neoeng_dcore_view_reference
  vendor/year2/src/visibility_buffer.cpp
)
add_library(NeoEng::DCoreViewReference ALIAS neoeng_dcore_view_reference)
target_include_directories(neoeng_dcore_view_reference
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/year2/include>
)
neoeng_set_warnings(neoeng_dcore_view_reference)

add_library(neoeng_dcore_view_lab
  src/view_lab.cpp
)
add_library(NeoEng::DCoreViewLab ALIAS neoeng_dcore_view_lab)
target_include_directories(neoeng_dcore_view_lab
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(neoeng_dcore_view_lab
  PUBLIC neoeng_dcore neoeng_dcore_view_reference
)
neoeng_set_warnings(neoeng_dcore_view_lab)

if(NEOENG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  foreach(target neoeng_dcore_view_reference neoeng_dcore_view_lab)
    target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  endforeach()
endif()

add_executable(neoeng_dcore_view_lab_cli apps/view_lab_cli.cpp)
target_link_libraries(neoeng_dcore_view_lab_cli PRIVATE neoeng_dcore_view_lab)
neoeng_set_warnings(neoeng_dcore_view_lab_cli)

if(BUILD_TESTING)
  add_executable(neoeng_dcore_view_reference_tests tests/visibility_reference_tests.cpp)
  target_link_libraries(neoeng_dcore_view_reference_tests PRIVATE neoeng_dcore_view_reference)
  neoeng_set_warnings(neoeng_dcore_view_reference_tests)
  add_test(NAME neoeng_dcore_view_reference_tests COMMAND neoeng_dcore_view_reference_tests)

  add_executable(neoeng_dcore_view_lab_integration_tests tests/view_lab_integration_tests.cpp)
  target_link_libraries(neoeng_dcore_view_lab_integration_tests PRIVATE neoeng_dcore_view_lab)
  neoeng_set_warnings(neoeng_dcore_view_lab_integration_tests)
  add_test(NAME neoeng_dcore_view_lab_integration_tests COMMAND neoeng_dcore_view_lab_integration_tests)

  add_test(NAME neoeng_dcore_view_lab_cli_smoke
    COMMAND neoeng_dcore_view_lab_cli
      ${CMAKE_BINARY_DIR}/neoeng-dcore-view-lab-smoke
      VIRTUALIZED-CTEST)
  set_tests_properties(
    neoeng_dcore_view_reference_tests
    neoeng_dcore_view_lab_integration_tests
    neoeng_dcore_view_lab_cli_smoke
    PROPERTIES LABELS "view-lab;dcore;smoke")
endif()

# NEOENG_WINDOWS_CLANG_BUILTINS_BEGIN
# Runtime necessário para a divisão inteira de 128 bits usada pelo View Lab.
if(
  WIN32
  AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
  AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
)
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
    RESULT_VARIABLE NEOENG_CLANG_RESOURCE_RESULT
    OUTPUT_VARIABLE NEOENG_CLANG_RESOURCE_DIR
    ERROR_VARIABLE NEOENG_CLANG_RESOURCE_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(NOT NEOENG_CLANG_RESOURCE_RESULT EQUAL 0)
    message(FATAL_ERROR "clang-cl resource directory failed: ${NEOENG_CLANG_RESOURCE_ERROR}")
  endif()

  set(
    NEOENG_CLANG_BUILTINS_LIBRARY
    "${NEOENG_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.builtins-x86_64.lib"
  )

  if(NOT EXISTS "${NEOENG_CLANG_BUILTINS_LIBRARY}")
    message(FATAL_ERROR "compiler-rt builtins not found: ${NEOENG_CLANG_BUILTINS_LIBRARY}")
  endif()

  target_link_libraries(
    neoeng_dcore_view_reference
    PUBLIC
      "${NEOENG_CLANG_BUILTINS_LIBRARY}"
  )
endif()
# NEOENG_WINDOWS_CLANG_BUILTINS_END
