cmake_minimum_required(VERSION 3.0)
project(epoll_svc)
include_directories(../core ../include)
IF (NOT WIN32)
set(CMAKE_CXX_FLAGS "-std=c++11 -pthread -g -Wall ${CMAKE_CXX_FLAGS}")
ENDIF ()
link_directories(${PROJECT_SOURCE_DIR}/../lib)
# -lrt for timer support
#link_libraries(jsoncpp)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/../bin)

#add_definitions(-D GEVENT_DUMP)
add_executable (epoll_svc epoll_svc.cpp svc_handler.cpp ../core/EventBase.cpp ../core/EventHandler.cpp ../core/log.cpp)
#target_compile_definitions(epoll_svc PUBLIC)
IF (WIN32)
target_link_libraries(epoll_svc jsoncpp ws2_32)
ELSE ()
target_link_libraries(epoll_svc jsoncpp rt)
ENDIF ()

# may pollute epoll_svc target, using target_compile_definitions
#add_definitions(-D TEST_TIMER)
add_executable (epoll_clt epoll_clt.cpp clt_handler.cpp ../core/EventBase.cpp ../core/EventBaseAR.cpp ../core/EventHandler.cpp ../core/log.cpp)
target_compile_definitions(epoll_clt PUBLIC -D TEST_READ)
IF (WIN32)
target_link_libraries(epoll_clt jsoncpp ws2_32)
ELSE ()
target_link_libraries(epoll_clt jsoncpp rt)
ENDIF ()

add_executable (epoll_local epoll_local.cpp)
#target_compile_definitions(epoll_local PUBLIC -D TEST_READ)
IF (WIN32)
target_link_libraries(epoll_local jsoncpp ws2_32)
ELSE ()
target_link_libraries(epoll_local jsoncpp rt)
ENDIF ()
