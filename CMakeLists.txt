cmake_minimum_required(VERSION 3.10)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_INSTALL_MESSAGE LAZY)  # skip "Up-to-date" messages for install function

project(snowBox)

# set paths
set(L0_PATH ${CMAKE_SOURCE_DIR}/level_zero)
set(L0_PATH ${CMAKE_SOURCE_DIR}/level_zero)
set(L0_INCLUDE ${L0_PATH}/include/level_zero)
set(SRC_PATH ${CMAKE_SOURCE_DIR}/src)
set(SRC_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/include)
set(OCLOC_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/ocloc/include)
set(TBB_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/tbb/include)
set(KERNELS_PATH ${CMAKE_SOURCE_DIR}/kernels)
set(DLLS_PATH ${CMAKE_SOURCE_DIR}/dlls)

set(MAIN_SRC  ${CMAKE_SOURCE_DIR}/src/main.cpp)

file(GLOB_RECURSE SRC_FILES
    "${SRC_PATH}/*.cpp"
    "${SRC_INCLUDE_PATH}/ze_info/*.hpp"
)

# include libraries
if(LINUX)
message(STATUS ">>> Linux")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread")
find_library(ZE_LIBS NAMES libze_loader.so libze_tracing_layer.so libze_validation_layer.so PATHS ${L0_PATH}/linux/lib/)
find_library(OCLOC_LIB NAMES ocloc PATHS ${CMAKE_SOURCE_DIR}/ocloc/linux/)
find_library(TBB_LIBS NAMES tbb12 tbb PATHS ${CMAKE_SOURCE_DIR}/tbb/linux/lib/)
find_library(TBB_LIBS_DEBUG NAMES  tbb12_debug tbb_debug PATHS ${CMAKE_SOURCE_DIR}/tbb/linux/lib/)
else()
message(STATUS ">>> Windows")
find_library(ZE_LIBS NAMES ze_loader ze_tracing_layer ze_validation_layer PATHS ${L0_PATH}/windows/lib/)
find_library(OCLOC_LIB NAMES ocloc64 PATHS ${CMAKE_SOURCE_DIR}/ocloc/windows)
find_library(TBB_LIBS NAMES tbb12 tbb PATHS ${CMAKE_SOURCE_DIR}/tbb/windows/lib/)
find_library(TBB_LIBS_DEBUG NAMES  tbb12_debug tbb_debug PATHS ${CMAKE_SOURCE_DIR}/tbb/windows/lib/)
endif()
set(APP_NAME sand_box)

add_executable(${APP_NAME} ${SRC_FILES} )

target_include_directories(${APP_NAME} PUBLIC ${SRC_INCLUDE_PATH} ${L0_INCLUDE} ${OCLOC_INCLUDE_PATH} ${TBB_INCLUDE_PATH})

target_link_libraries(${APP_NAME} general ${ZE_LIBS} ${OCLOC_LIB})
target_link_libraries(${APP_NAME} optimized ${TBB_LIBS} debug ${TBB_LIBS_DEBUG})

if(LINUX)

else()
 add_custom_command(TARGET "${APP_NAME}" POST_BUILD
      COMMAND if $<CONFIG:Debug> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${KERNELS_PATH}/module.cl" "${CMAKE_BINARY_DIR}/Debug/")
      COMMAND if $<CONFIG:Debug> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${DLLS_PATH}/tbb12_debug.dll" "${CMAKE_BINARY_DIR}/Debug/")
      COMMAND if $<CONFIG:Debug> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${DLLS_PATH}/ocloc64.dll" "${CMAKE_BINARY_DIR}/Debug/")
      COMMAND if $<CONFIG:Release> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${KERNELS_PATH}/module.cl" "${CMAKE_BINARY_DIR}/Release/")
      COMMAND if $<CONFIG:Release> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${DLLS_PATH}/tbb12.dll" "${CMAKE_BINARY_DIR}/Release/")
      COMMAND if $<CONFIG:Release> neq 0 ("${CMAKE_COMMAND}" -E copy_if_different "${DLLS_PATH}/ocloc64.dll" "${CMAKE_BINARY_DIR}/Release/")
      COMMENT "Copying Kernels to output..."
    )
endif()
