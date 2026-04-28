cmake_minimum_required(VERSION 3.0)
project(video_server)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS  "$ENV{CXXFLAGS} \
    -rdynamic \
    -std=c++17 \
    -fPIC \
    -g \
    -Wall \
    -Wno-deprecated \
    -Werror \
    -Wno-unused-function \
    -Wno-deprecated-declarations \
    -Wno-sign-compare \
    -Wno-builtin-macro-redefined\
    -Wno-unused-local-typedefs\
    -Wno-sequence-point \
    -Wcast-align")


# 测试用例默认关闭
option(MUDUO_TEST "make muduo test file" OFF)
option(WORK_TEST "make work test file" OFF)
option(MEM_CHECK "make memory check flag" OFF)
option(COVERAGE_TEST "make coverage file" OFF)

# ASan环境变量 首个错误打断+检测泄漏+详细输出+快速退出+日志路径
# export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:verbosity=1:log_path=/mnt/nfs/proxy_log/kit_asan.log:hard_rss_limit_mb=4096

if(COVERAGE_TEST)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
elseif(MEM_CHECK)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fsanitize=alignment,address -fno-omit-frame-pointer")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

# 添加第三方包搜索路径
list(APPEND CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/pkg")
# gtest包添加
find_package(GTest REQUIRED)
enable_testing()

# 添加第三方库链接路径
set(PKG_LIB_PATH
    "pkg/llhttp/lib"
)

link_directories(${PKG_LIB_PATH})

# include
include_directories(
    ${GTEST_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/work/
    ${PROJECT_SOURCE_DIR}/include/work/service/
    ${PROJECT_SOURCE_DIR}/include/work/repository/
    ${PROJECT_SOURCE_DIR}/include/work/dao/

    ${PROJECT_SOURCE_DIR}/pkg/gtest/include
    ${PROJECT_SOURCE_DIR}/pkg/llhttp/include
    ${PROJECT_SOURCE_DIR}/pkg/
    ${PROJECT_SOURCE_DIR}/include/db/sqlite3/
    ${PROJECT_SOURCE_DIR}/include/db/sqlite3/
    ${PROJECT_SOURCE_DIR}/pkg/nanoid_cpp/include/
)

set(BASE_SRC
    src/base/log.cpp
    src/base/log_level.cpp
    src/base/log_formatter.cpp
    src/base/log_attr.cpp
    src/base/log_appender.cpp

    src/base/util.cpp
    src/base/time_stamp.cpp
    src/base/content_parser.cpp
    # src/base/multi_form_data_parser.cpp

    src/base/thread.cpp
    src/base/thread_pool.cpp
    src/base/event_loop_thread.cpp
    src/base/event_loop_thread_pool.cpp

)

set(NET_HTTP_SRC
    src/net/http/http_request.cpp
    src/net/http/http_context.cpp
    src/net/http/http_response.cpp
    src/net/http/http_server.cpp
    src/net/http/http_custom_parser.cpp
    src/net/http/http_llhttp_parser.cpp
    src/net/http/http_router.cpp
    src/net/http/http_servlet.cpp
    src/net/http/http_util.cpp
)


set(NET_SRC
    src/net/inet_address.cpp
    src/net/channel.cpp
    src/net/poller.cpp
    src/net/default_poller.cpp
    src/net/epoll_poller.cpp
    src/net/poll_poller.cpp
    src/net/event_loop.cpp
    src/net/socket.cpp
    src/net/acceptor.cpp
    src/net/tcp_server.cpp
    src/net/buffer.cpp
    src/net/tcp_connection.cpp
    src/net/timer.cpp
    src/net/sample_timer_queue.cpp
    src/net/net_data_converter.cpp
    src/net/udp_datagram.cpp
    src/net/udp_server.cpp
    
    ${NET_HTTP_SRC}
)


set(MUDUO_SRC
    ${BASE_SRC}
    ${NET_SRC}
)


set(WORK_SRC

)

# defines
set(DEF
    # ${SQLITE3_DEF}
)

add_library(kit_muduo SHARED ${MUDUO_SRC})
redefine_file_macro(kit_muduo)   #重定义 __FILE__


# lib 
set(LIB_LIB
    kit_muduo
    dl
    pthread
    GTest::gtest 
    GTest::gtest_main 
    GTest::gmock 
    GTest::gmock_main
    llhttp
)

set(DEPENDS
    kit_muduo
)

#dependency
set(DEPENDS
    kit_muduo
)

# **********************************tests**********************************#

if(MUDUO_TEST)

    # time_stamp  时间戳测试 test_time_stamp
    add_executable(test_time_stamp tests/test_time_stamp.cpp)
    add_dependencies(test_time_stamp ${DEPENDS})
    redefine_file_macro(test_time_stamp)   #重定义 __FILE__
    target_link_libraries(test_time_stamp ${LIB_LIB})

    # inet_address 网络地址测试 test_inet_address
    add_executable(test_inet_address tests/test_inet_address.cpp)
    add_dependencies(test_inet_address ${DEPENDS})
    redefine_file_macro(test_inet_address)   #重定义 __FILE__
    target_link_libraries(test_inet_address ${LIB_LIB})

    # Thread 线程测试 test_thread
    add_executable(test_thread tests/test_thread.cpp)
    add_dependencies(test_thread ${DEPENDS})
    redefine_file_macro(test_thread)   #重定义 __FILE__
    target_link_libraries(test_thread ${LIB_LIB})

    # EventLoopThread 线程测试 test_event_loop_thread
    add_executable(test_event_loop_thread tests/test_event_loop_thread.cpp)
    add_dependencies(test_event_loop_thread ${DEPENDS})
    redefine_file_macro(test_event_loop_thread)   #重定义 __FILE__
    target_link_libraries(test_event_loop_thread ${LIB_LIB})

    # socket测试 test_socket
    add_executable(test_socket tests/test_socket.cpp)
    add_dependencies(test_socket ${DEPENDS})
    redefine_file_macro(test_socket)   #重定义 __FILE__
    target_link_libraries(test_socket ${LIB_LIB})

    # Buffer测试 test_buffer
    add_executable(test_buffer tests/test_buffer.cpp)
    add_dependencies(test_buffer ${DEPENDS})
    redefine_file_macro(test_buffer)   #重定义 __FILE__
    target_link_libraries(test_buffer ${LIB_LIB})

    # Timer测试 test_timer
    add_executable(test_timer tests/test_timer.cpp)
    add_dependencies(test_timer ${DEPENDS})
    redefine_file_macro(test_timer)   #重定义 __FILE__
    target_link_libraries(test_timer ${LIB_LIB})

    # TcpServer测试 test_tcp_server
    add_executable(test_tcp_server tests/test_tcp_server.cpp)
    add_dependencies(test_tcp_server ${DEPENDS})
    redefine_file_macro(test_tcp_server)   #重定义 __FILE__
    target_link_libraries(test_tcp_server ${LIB_LIB})

    # http测试 test_http
    add_executable(test_http tests/http/test_http.cpp)
    add_dependencies(test_http ${DEPENDS})
    redefine_file_macro(test_http)   #重定义 __FILE__
    target_link_libraries(test_http ${LIB_LIB})

    # llhttp测试 test_llhttp
    add_executable(test_llhttp tests/http/test_llhttp.cpp)
    add_dependencies(test_llhttp ${DEPENDS})
    redefine_file_macro(test_llhttp)   #重定义 __FILE__
    target_link_libraries(test_llhttp ${LIB_LIB})

    # thread_pool 测试 test_thread_pool
    add_executable(test_thread_pool tests/test_thread_pool.cpp ${WORK_SRC})
    add_dependencies(test_thread_pool ${DEPENDS})
    redefine_file_macro(test_thread_pool)   #重定义 __FILE__
    target_link_libraries(test_thread_pool ${LIB_LIB})


    # 格式转换 测试 test_convert
    add_executable(test_convert tests/test_convert.cpp ${WORK_SRC})
    add_dependencies(test_convert ${DEPENDS})
    redefine_file_macro(test_convert)   #重定义 __FILE__
    target_link_libraries(test_convert ${LIB_LIB})

    add_executable(test_router tests/http/test_router.cpp)
    add_dependencies(test_router ${DEPENDS})
    redefine_file_macro(test_router)   #重定义 __FILE__
    target_link_libraries(test_router ${LIB_LIB})

    #variant 自定义类型包装器
    add_executable(test_variant tests/test_variant.cpp ${WORK_SRC})
    add_dependencies(test_variant ${DEPENDS})
    redefine_file_macro(test_variant)   #重定义 __FILE__
    target_compile_definitions(test_variant PRIVATE ${DEF})
    target_link_libraries(test_variant ${LIB_LIB})

    
    #test_asan 内存泄漏工具
    add_executable(test_asan tests/test_asan.cpp ${WORK_SRC})
    add_dependencies(test_asan ${DEPENDS})
    redefine_file_macro(test_asan)   #重定义 __FILE__
    target_link_libraries(test_asan ${LIB_LIB})

    #test_udp udp集成测试
    add_executable(test_udp tests/test_udp.cpp ${WORK_SRC})
    add_dependencies(test_udp ${DEPENDS})
    redefine_file_macro(test_udp)   #重定义 __FILE__
    target_link_libraries(test_udp ${LIB_LIB})
    add_test(NAME test_udp COMMAND test_udp)


    #test_lru_cache LRU缓存测试
    add_executable(test_lru_cache tests/test_lru_cache.cpp ${WORK_SRC})
    add_dependencies(test_lru_cache ${DEPENDS})
    redefine_file_macro(test_lru_cache)   #重定义 __FILE__
    target_link_libraries(test_lru_cache ${LIB_LIB})
    add_test(NAME test_lru_cache COMMAND test_lru_cache)

endif()



# **********************************example**********************************#
# http服务器实例
add_executable(example_http_server example/example_http_server.cpp)
add_dependencies(example_http_server ${DEPENDS})
redefine_file_macro(example_http_server)   #重定义 __FILE__
target_link_libraries(example_http_server ${LIB_LIB})


if(COVERAGE_TEST)
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/coverage)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib/coverage)
else()
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
    set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
endif()
