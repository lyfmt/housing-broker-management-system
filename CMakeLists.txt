cmake_minimum_required(VERSION 3.10)

project(housing_broker_management_system C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(rental_system
    src/app/main.c
    src/core/rental_system.c
    src/core/storage.c
    src/modules/bootstrap_data.c
    src/modules/demo_data.c
    src/modules/password_utils.c
    src/modules/domain_utils.c
    src/modules/login_guard.c
    src/modules/data_path_utils.c
)

target_include_directories(rental_system PRIVATE include)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rental_system PRIVATE -Wall -Wextra -Wpedantic)
endif()
