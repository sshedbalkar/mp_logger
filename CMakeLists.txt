cmake_minimum_required(VERSION 3.20)

project(mp_logger VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

function(mp_logger_enable_warnings target_name)
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_library(mp_logger
  src/mp_logger.c
  src/mp_logger_bootstrap.c
  src/mp_logger_streams.c
)

target_compile_features(mp_logger PUBLIC c_std_17)
target_include_directories(mp_logger
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mp_logger PUBLIC Threads::Threads)
mp_logger_enable_warnings(mp_logger)
set_target_properties(mp_logger PROPERTIES
  OUTPUT_NAME mp_logger
  POSITION_INDEPENDENT_CODE ON
)

option(MP_LOGGER_BUILD_TESTS "Build mp_logger tests" ON)
if(MP_LOGGER_BUILD_TESTS)
  enable_testing()
  add_executable(mp_logger_tests tests/mp_logger_tests.c)
  target_link_libraries(mp_logger_tests PRIVATE mp_logger)
  mp_logger_enable_warnings(mp_logger_tests)
  add_test(NAME mp_logger COMMAND mp_logger_tests)
endif()

option(MP_LOGGER_BUILD_BENCHMARKS "Build mp_logger benchmarks" ON)
if(MP_LOGGER_BUILD_BENCHMARKS)
  add_executable(mp_logger_benchmarks tests/mp_logger_benchmarks.c)
  target_include_directories(mp_logger_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(mp_logger_benchmarks PRIVATE mp_logger)
  mp_logger_enable_warnings(mp_logger_benchmarks)
endif()

configure_package_config_file(
  cmake/mp_loggerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/mp_loggerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mp_logger
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/mp_loggerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(TARGETS mp_logger
  EXPORT mp_loggerTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES configs/logger.bootstrap.yaml DESTINATION ${CMAKE_INSTALL_DATADIR}/mp_logger)
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mp_loggerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mp_loggerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mp_logger
)
install(
  EXPORT mp_loggerTargets
  FILE mp_loggerTargets.cmake
  NAMESPACE mp_logger::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mp_logger
)
