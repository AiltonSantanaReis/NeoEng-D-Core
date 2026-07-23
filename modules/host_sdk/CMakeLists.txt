add_library(neoeng_dcore_host_sdk STATIC src/dcore_host.cpp)
add_library(NeoEng::DCoreHostSdk ALIAS neoeng_dcore_host_sdk)
set_target_properties(neoeng_dcore_host_sdk PROPERTIES
  EXPORT_NAME DCoreHostSdk
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(neoeng_dcore_host_sdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(neoeng_dcore_host_sdk PUBLIC neoeng_dcore)
neoeng_set_warnings(neoeng_dcore_host_sdk)
if(NEOENG_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  target_compile_options(neoeng_dcore_host_sdk PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(neoeng_dcore_host_sdk PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(BUILD_TESTING)
  add_executable(neoeng_host_sdk_tests tests/host_sdk_tests.cpp)
  target_link_libraries(neoeng_host_sdk_tests PRIVATE neoeng_dcore_host_sdk)
  neoeng_set_warnings(neoeng_host_sdk_tests)
  add_test(NAME neoeng_host_sdk_tests COMMAND neoeng_host_sdk_tests)

  add_executable(neoeng_host_sdk_c_header_test tests/host_sdk_header_c.c)
  target_link_libraries(neoeng_host_sdk_c_header_test PRIVATE neoeng_dcore_host_sdk)
  set_target_properties(neoeng_host_sdk_c_header_test PROPERTIES LINKER_LANGUAGE CXX)
  if(MSVC)
    target_compile_options(neoeng_host_sdk_c_header_test PRIVATE /W4 /WX)
  else()
    target_compile_options(neoeng_host_sdk_c_header_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME neoeng_host_sdk_c_header_test COMMAND neoeng_host_sdk_c_header_test)

  add_executable(neoeng_host_sdk_reference apps/host_sdk_reference.c)
  target_link_libraries(neoeng_host_sdk_reference PRIVATE neoeng_dcore_host_sdk)
  set_target_properties(neoeng_host_sdk_reference PROPERTIES LINKER_LANGUAGE CXX)
  if(MSVC)
    target_compile_options(neoeng_host_sdk_reference PRIVATE /W4 /WX)
  else()
    target_compile_options(neoeng_host_sdk_reference PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME neoeng_host_sdk_reference COMMAND neoeng_host_sdk_reference)

  set_tests_properties(
    neoeng_host_sdk_tests neoeng_host_sdk_c_header_test neoeng_host_sdk_reference
    PROPERTIES LABELS "smoke;dcore;host-sdk")
endif()
