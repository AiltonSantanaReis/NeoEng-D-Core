if(NOT DEFINED NEOENG_SOURCE_DIR OR NOT DEFINED NEOENG_BUILD_DIR)
  message(FATAL_ERROR "NEOENG_SOURCE_DIR and NEOENG_BUILD_DIR are required")
endif()
# Keep the nested consumer build short enough for Windows rc.exe and Ninja.
# The repository path may already be close to MAX_PATH and may contain UTF-8.
set(prefix "${NEOENG_SOURCE_DIR}/tmp/sdk-i")
set(consumer_build "${NEOENG_SOURCE_DIR}/tmp/sdk-c")
file(REMOVE_RECURSE "${prefix}" "${consumer_build}")

set(config_args)
if(DEFINED NEOENG_TEST_CONFIG AND NOT NEOENG_TEST_CONFIG STREQUAL "")
  list(APPEND config_args --config "${NEOENG_TEST_CONFIG}")
endif()
# NEOENG_WINDOWS_VCPKG_CONSUMER_BEGIN
# O consumidor é configurado em uma árvore CMake independente.
# Propaga o prefixo instalado do triplet, sem usar vcpkg/pkgs.
if(WIN32)
    file(
        GLOB
        _neoeng_vcpkg_candidates
        LIST_DIRECTORIES true
        "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed/*"
    )

    set(_neoeng_vcpkg_triplet_prefix "")

    foreach(_neoeng_candidate IN LISTS _neoeng_vcpkg_candidates)
        if(
            IS_DIRECTORY "${_neoeng_candidate}/include"
            AND (
                EXISTS
                    "${_neoeng_candidate}/share/boost/BoostConfig.cmake"
                OR EXISTS
                    "${_neoeng_candidate}/share/boost/boost-config.cmake"
            )
        )
            set(
                _neoeng_vcpkg_triplet_prefix
                "${_neoeng_candidate}"
            )
            break()
        endif()
    endforeach()

    if(
        _neoeng_vcpkg_triplet_prefix STREQUAL ""
        AND DEFINED NEOENG_BOOST_DIR
        AND IS_DIRECTORY "${NEOENG_BOOST_DIR}"
    )
        get_filename_component(
            _neoeng_vcpkg_share_prefix
            "${NEOENG_BOOST_DIR}"
            DIRECTORY
        )
        get_filename_component(
            _neoeng_vcpkg_triplet_prefix
            "${_neoeng_vcpkg_share_prefix}"
            DIRECTORY
        )
    endif()

    if(_neoeng_vcpkg_triplet_prefix STREQUAL "")
        message(
            FATAL_ERROR
            "Installed vcpkg triplet containing Boost was not found"
        )
    endif()

    if(
        DEFINED ENV{CMAKE_PREFIX_PATH}
        AND NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL ""
    )
        set(
            ENV{CMAKE_PREFIX_PATH}
            "${_neoeng_vcpkg_triplet_prefix};$ENV{CMAKE_PREFIX_PATH}"
        )
    else()
        set(
            ENV{CMAKE_PREFIX_PATH}
            "${_neoeng_vcpkg_triplet_prefix}"
        )
    endif()
endif()
# NEOENG_WINDOWS_VCPKG_CONSUMER_END
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${NEOENG_BUILD_DIR}" --prefix "${prefix}" ${config_args}
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "install failed\n${install_output}\n${install_error}")
endif()

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${NEOENG_SOURCE_DIR}/tests/cmake/host_sdk_consumer"
  -B "${consumer_build}"
  -G "${NEOENG_CMAKE_GENERATOR}"
  "-DCMAKE_PREFIX_PATH=${prefix}"
  "-DCMAKE_C_COMPILER=${NEOENG_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER=${NEOENG_CXX_COMPILER}")
if(
  DEFINED NEOENG_TEST_CONFIG
  AND NOT NEOENG_TEST_CONFIG STREQUAL ""
  AND NOT NEOENG_CMAKE_GENERATOR MATCHES "Visual Studio|Xcode|Multi-Config"
)
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${NEOENG_TEST_CONFIG}")
endif()
execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "consumer configure failed\n${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args}
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "consumer build failed\n${build_output}\n${build_error}")
endif()

# NEOENG_CONSUMER_EXECUTABLE_PATH_BEGIN
# Ninja é single-config e grava o executável diretamente em consumer_build.
# Geradores multi-config podem gravá-lo em consumer_build/<config>.
set(executable_candidates)

if(WIN32)
    if(
        DEFINED NEOENG_TEST_CONFIG
        AND NOT NEOENG_TEST_CONFIG STREQUAL ""
    )
        list(
            APPEND
            executable_candidates
            "${consumer_build}/${NEOENG_TEST_CONFIG}/neoeng_dcore_installed_consumer.exe"
        )
    endif()

    list(
        APPEND
        executable_candidates
        "${consumer_build}/neoeng_dcore_installed_consumer.exe"
    )
else()
    if(
        DEFINED NEOENG_TEST_CONFIG
        AND NOT NEOENG_TEST_CONFIG STREQUAL ""
    )
        list(
            APPEND
            executable_candidates
            "${consumer_build}/${NEOENG_TEST_CONFIG}/neoeng_dcore_installed_consumer"
        )
    endif()

    list(
        APPEND
        executable_candidates
        "${consumer_build}/neoeng_dcore_installed_consumer"
    )
endif()

set(executable "")

foreach(candidate IN LISTS executable_candidates)
    if(EXISTS "${candidate}")
        set(executable "${candidate}")
        break()
    endif()
endforeach()

if(executable STREQUAL "")
    string(
        JOIN
        "\n  "
        candidate_list
        ${executable_candidates}
    )

    message(
        FATAL_ERROR
        "Installed consumer executable was not found. Candidates:\n  ${candidate_list}"
    )
endif()
# NEOENG_CONSUMER_EXECUTABLE_PATH_END
execute_process(
  COMMAND "${executable}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "installed consumer failed\n${run_output}\n${run_error}")
endif()

set(distributed_executable_candidates)
if(WIN32)
  if(DEFINED NEOENG_TEST_CONFIG AND NOT NEOENG_TEST_CONFIG STREQUAL "")
    list(APPEND distributed_executable_candidates
      "${consumer_build}/${NEOENG_TEST_CONFIG}/neoeng_dcore_distributed_installed_consumer.exe")
  endif()
  list(APPEND distributed_executable_candidates
    "${consumer_build}/neoeng_dcore_distributed_installed_consumer.exe")
else()
  if(DEFINED NEOENG_TEST_CONFIG AND NOT NEOENG_TEST_CONFIG STREQUAL "")
    list(APPEND distributed_executable_candidates
      "${consumer_build}/${NEOENG_TEST_CONFIG}/neoeng_dcore_distributed_installed_consumer")
  endif()
  list(APPEND distributed_executable_candidates
    "${consumer_build}/neoeng_dcore_distributed_installed_consumer")
endif()
set(distributed_executable "")
foreach(candidate IN LISTS distributed_executable_candidates)
  if(EXISTS "${candidate}")
    set(distributed_executable "${candidate}")
    break()
  endif()
endforeach()
if(distributed_executable STREQUAL "")
  message(FATAL_ERROR "installed distributed-reference consumer executable was not found")
endif()
execute_process(
  COMMAND "${distributed_executable}"
  RESULT_VARIABLE distributed_run_result
  OUTPUT_VARIABLE distributed_run_output
  ERROR_VARIABLE distributed_run_error)
if(NOT distributed_run_result EQUAL 0)
  message(FATAL_ERROR
    "installed distributed-reference consumer failed\n"
    "${distributed_run_output}\n${distributed_run_error}")
endif()
file(REMOVE_RECURSE "${prefix}" "${consumer_build}")
message(STATUS "installed NeoEng D-Core host SDK and distributed-reference consumers passed")
