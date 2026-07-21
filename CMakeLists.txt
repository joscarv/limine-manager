cmake_minimum_required(VERSION 3.20)
project(limine-manager VERSION 1.6.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(LIMINE_MANAGER_BUILD_TESTS "Build tests" ON)
option(LIMINE_MANAGER_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LIMINE_MANAGER_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LIMINE_MANAGER_ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation" OFF)
option(LIMINE_MANAGER_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
set(LIMINE_MANAGER_CLANG_TIDY "clang-tidy" CACHE STRING "clang-tidy executable")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(limine_manager_apply_quality TARGET_NAME)
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)
    set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    )

    set(_sanitizers "")
    if(LIMINE_MANAGER_ENABLE_ASAN)
        list(APPEND _sanitizers address)
    endif()
    if(LIMINE_MANAGER_ENABLE_UBSAN)
        list(APPEND _sanitizers undefined)
    endif()
    if(_sanitizers AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        list(JOIN _sanitizers "," _sanitizer_list)
        target_compile_options(${TARGET_NAME} PRIVATE -fno-omit-frame-pointer -fsanitize=${_sanitizer_list})
        target_link_options(${TARGET_NAME} PRIVATE -fno-omit-frame-pointer -fsanitize=${_sanitizer_list})
    endif()

    if(LIMINE_MANAGER_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE -O0 -g --coverage)
        target_link_options(${TARGET_NAME} PRIVATE --coverage)
    endif()
endfunction()

function(limine_manager_add_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE limine_manager_core)
    limine_manager_apply_quality(${TARGET_NAME})
    target_compile_options(${TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

if(LIMINE_MANAGER_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES "${LIMINE_MANAGER_CLANG_TIDY}" REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
endif()

add_library(limine_manager_core
    src/application/discovery_service.cpp
    src/application/preview_service.cpp
    src/application/status_service.cpp
    src/application/change_planner.cpp
    src/application/apply_service.cpp
    src/application/secure_boot_apply_service.cpp
    src/application/secure_boot_transaction.cpp
    src/application/automation.cpp
    src/application/backup_service.cpp
    src/application/rollback_planner.cpp
    src/application/rollback_service.cpp
    src/application/refresh_service.cpp
    src/application/validation_service.cpp
    src/domain/kernel_cmdline.cpp
    src/domain/menu.cpp
    src/domain/rollback.cpp
    src/domain/validation.cpp
    src/config/config_loader.cpp
    src/config/theme.cpp
    src/infrastructure/filesystem.cpp
    src/infrastructure/btrfs_client.cpp
    src/infrastructure/efi_image_transaction.cpp
    src/infrastructure/kernel_discovery.cpp
    src/infrastructure/process.cpp
    src/infrastructure/secure_boot_tools.cpp
    src/infrastructure/secure_file_ops.cpp
