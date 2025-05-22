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

set(BASE_SRC
    src/base/log.cpp
    src/base/log_level.cpp
    src/base/log_formatter.cpp
    src/base/log_attr.cpp
    src/base/log_appender.cpp

    src/base/util.cpp
    src/base/time_stamp.cpp

    src/base/thread.cpp
    src/base/event_loop_thread.cpp
    src/base/event_loop_thread_pool.cpp
)

set(NET_SRC
    src/net/inet_address.cpp
    src/net/channel.cpp
    src/net/poller.cpp
    src/net/default_poller.cpp
    src/net/epoll_poller.cpp
    src/net/event_loop.cpp

)

set(SRC
    ${BASE_SRC}
    ${NET_SRC}
)

add_library(kit_muduo SHARED ${SRC})
redefine_file_macro(kit_muduo)   #重定义 __FILE__

set(LIB_LIB
    kit_muduo
    dl
    pthread
    GTest::GTest
    GTest::Main
)

set(DEPENDS
    kit_muduo
)


# **********************************tests**********************************#
if(MUDUO_TEST)
    # muduo库使用示例 test_muduo
    add_executable(test_muduo tests/test_muduo.cpp)
    target_link_directories(test_muduo PRIVATE ${EXTERN_LIB_PATH})
    redefine_file_macro(test_muduo)   #重定义 __FILE__
    target_link_libraries(test_muduo ${LIB_LIB} ${MUDUO_LIB})

    # time_stamp  时间戳测试 test_time_stamp
    add_executable(test_time_stamp tests/test_time_stamp.cpp)
    redefine_file_macro(test_time_stamp)   #重定义 __FILE__
    target_link_libraries(test_time_stamp ${LIB_LIB})

    # inet_address 网络地址测试 test_inet_address
    add_executable(test_inet_address tests/test_inet_address.cpp)
    redefine_file_macro(test_inet_address)   #重定义 __FILE__
    target_link_libraries(test_inet_address ${LIB_LIB})

    # Thread 线程测试 test_thread
    add_executable(test_thread tests/test_thread.cpp)
    redefine_file_macro(test_thread)   #重定义 __FILE__
    target_link_libraries(test_thread ${LIB_LIB})

    # EventLoopThread 线程测试 test_event_loop_thread
    add_executable(test_event_loop_thread tests/test_event_loop_thread.cpp)
    redefine_file_macro(test_event_loop_thread)   #重定义 __FILE__
    target_link_libraries(test_event_loop_thread ${LIB_LIB})
endif()


# **********************************example**********************************#


# 设置可执行文件路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置库输出路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

