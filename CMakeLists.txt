cmake_minimum_required (VERSION 3.0)
project (scout.cpp)


# adjust these if wabt or yaml-cpp are not subdirectories of this main repo directory
set(WABT_DIR ./)
set(YAMLCPP_DIR ./)


add_subdirectory(${WABT_DIR}/wabt)
add_subdirectory(${YAMLCPP_DIR}/yaml-cpp)


add_executable(scout.exec scout.cpp)
add_dependencies(scout.exec wabt yaml-cpp)
target_link_libraries(scout.exec wabt yaml-cpp)
include_directories(${CMAKE_BINARY_DIR}/wabt ${WABT_DIR}/wabt ${WABT_DIR}/wabt/src ${YAMLCPP_DIR}/yaml-cpp/include ${YAMLCPP_DIR}/yaml-cpp/src)
