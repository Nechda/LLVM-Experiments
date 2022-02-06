cmake_minimum_required(VERSION 3.10)
project(ll-ex)

find_package(LLVM REQUIRED PATHS ${LLVM_DIR} CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(func_attributes examples/add_function_attributes.cpp)
add_executable(optimize_module examples/optimize_module.cpp)
add_executable(read_meta examples/read_meta.cpp)


# dont work at llvm 10
# llvm_map_components_to_libnames(llvm_libs all)

llvm_map_components_to_libnames(llvm_libs core support irreader passes)

message(STATUS "${llvm_libs}")

# Link against LLVM libraries
target_link_libraries(func_attributes ${llvm_libs})
target_link_libraries(optimize_module ${llvm_libs})
target_link_libraries(read_meta ${llvm_libs})