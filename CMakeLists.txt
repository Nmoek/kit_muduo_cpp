cmake_minimum_required(VERSION 3.0)
project(muduo手写网络库)


set(CXX_STANDARD 17)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)


set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O3  --std=c++17 -fPIC -ggdb -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations -Wno-sign-compare -Wno-unused-result")

# 测试用例默认关闭
option(MUDUO_TEST "make test file" OFF)


# 查找gtest库
find_package(GTest REQUIRED)


include_directories(
    include
    ${PROJECT_SOURCE_DIR}/include/
)

link_directories(
    ${PROJECT_SOURCE_DIR}/lib
    ${PROJECT_SOURCE_DIR}/lib/muduo
)


set(EXTERN_LIB_PATH
    "${PROJECT_SOURCE_DIR}/lib/muduo/"
    "/use/local/lib/")

set(MUDUO_LIB
    muduo_net
    muduo_base
    muduo_http
    muduo_inspect
)


set(LIB_LIB
    dl
    pthread
    GTest::GTest
    GTest::Main
)

set(DEPENDS

)


# **********************************tests**********************************#
if(MUDUO_TEST)
    # muduo库使用示例 test_muduo
    add_executable(test_muduo tests/test_muduo.cpp)
    # add_dependencies(test_muduo ${DEPENDS})
    target_link_directories(test_muduo PRIVATE ${EXTERN_LIB_PATH})
    redefine_file_macro(test_muduo)   #重定义 __FILE__
    target_link_libraries(test_muduo ${LIB_LIB} ${MUDUO_LIB})

endif()


# **********************************example**********************************#


# 设置可执行文件路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置库输出路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

