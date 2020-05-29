cmake_minimum_required(VERSION 3.0)
project(epoll_svc)
include_directories(../core ../include)
set(CMAKE_CXX_FLAGS "-std=c++11 -pthread -g -Wall ${CMAKE_CXX_FLAGS}")
link_directories(${PROJECT_SOURCE_DIR}/../lib)
# -lrt for timer support
#link_libraries(jsoncpp.a)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../bin)

#add_definitions(-D IOCP_DUMP)
add_executable (epoll_svc epoll_svc.cpp svc_handler.cpp ../core/EventBase.cpp ../core/EventHandler.cpp ../core/log.cpp)
#target_compile_definitions(epoll_svc PUBLIC)
target_link_libraries(epoll_svc jsoncpp.a rt)

# may pollute epoll_svc target, using target_compile_definitions
#add_definitions(-D TEST_TIMER)
add_executable (epoll_clt epoll_clt.cpp clt_handler.cpp ../core/EventBase.cpp ../core/EventBaseAR.cpp ../core/EventHandler.cpp ../core/log.cpp)
target_compile_definitions(epoll_clt PUBLIC -D TEST_CONN)
target_link_libraries(epoll_clt jsoncpp.a rt)

add_executable (epoll_local epoll_local.cpp)
#target_compile_definitions(epoll_local PUBLIC -D TEST_READ)
target_link_libraries(epoll_local jsoncpp.a rt)
