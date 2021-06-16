cmake_minimum_required(VERSION 3.10)



set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_INSTALL_MESSAGE LAZY)  # skip "Up-to-date" messages for install function

project(snowBox)

# set paths
set(L0_PATH ${CMAKE_SOURCE_DIR}/level_zero)
set(L0_INCLUDE ${L0_PATH}/include/level_zero)
set(SRC_PATH ${CMAKE_SOURCE_DIR}/src)
set(SRC_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/include)
set(OCLOC_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/ocloc/include)
set(TBB_INCLUDE_PATH ${CMAKE_SOURCE_DIR}/tbb/include)


set(MAIN_SRC  ${CMAKE_SOURCE_DIR}/src/main.cpp)

file(GLOB_RECURSE SRC_FILES
    "${SRC_PATH}/*.cpp"
    "${SRC_PATH}/*.hpp"
)

# include libraries
find_library(ZE_LIBS NAMES ze_loader ze_tracing_layer ze_validation_layer PATHS ${L0_PATH}/lib/)
find_library(OCLOC_LIB NAMES ocloc64 PATHS ${CMAKE_SOURCE_DIR}/ocloc)
find_library(TBB_LIBS NAMES tbb12 tbb tbbbind tbbmalloc PATHS ${CMAKE_SOURCE_DIR}/tbb/lib/)
# find_library(TBB_DEBUG_LIBS NAMES  tbb12_debug tbb_debug tbbbind_debug tbbmalloc_debug PATHS ${CMAKE_SOURCE_DIR}/tbb/lib/)
 
set(APP_NAME sand_box)

add_executable(${APP_NAME} ${SRC_FILES} )

target_include_directories(${APP_NAME} PUBLIC ${SRC_INCLUDE_PATH} ${L0_INCLUDE} ${OCLOC_INCLUDE_PATH} ${TBB_INCLUDE_PATH})

# target_link_libraries(${APP_NAME} debug ${ZE_LIBS} ${OCLOC_LIB} ${TBB_DEBUG_LIBS})
target_link_libraries(${APP_NAME} optimized  ${ZE_LIBS} ${OCLOC_LIB} ${TBB_LIBS})