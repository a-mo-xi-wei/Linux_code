cmake_minimum_required(VERSION 3.10)

project(Http C)

set(CMAKE_C_STANDARD 11)

set(SOURCEFILE
    httpServer.c
)

add_executable(${PROJECT_NAME} ${SOURCEFILE})