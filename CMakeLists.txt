cmake_minimum_required(VERSION 3.5)
project(guidecam)

find_package(PkgConfig REQUIRED)
pkg_check_modules(INDI REQUIRED libindi)

include_directories(${INDI_INCLUDE_DIRS})
link_directories(${INDI_LIBRARY_DIRS})

add_executable(guidecam guidecam.cpp)
target_link_libraries(guidecam ${INDI_LIBRARIES} indidriver)

