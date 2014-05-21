project(proc_mon)
cmake_minimum_required(VERSION 2.8)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROCPS libprocps REQUIRED)
link_directories(${PROCPS_LIBRARY_DIRS})
LINK_LIBRARIES(${PROCPS_LIBRARIES})
aux_source_directory(. SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})

