cmake_minimum_required(VERSION 3.16)

project(GraduationProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(GraduationProject main.cpp
    helperFunctions.h
    helperFunctions.cpp)

include(GNUInstallDirs)
install(TARGETS GraduationProject
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
