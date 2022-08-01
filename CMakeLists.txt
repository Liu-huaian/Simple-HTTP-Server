#设置最低版本要求
cmake_minimum_required(VERSION 2.8)

#设置工程名称
project(WEB_SERVER)

#增加编译选项
add_compile_options(-Wall -g -std=c++11)

#设置生成的可执行文件保存的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

#1、读取目录下的所有源代码，存入到变量SRC中
aux_source_directory(${PROJECT_SOURCE_DIR}/src  SRC)
#2、将源代码所对应的头文件包含到路径中
include_directories(${PROJECT_SOURCE_DIR}/inc)
#3、生成可执行文件
add_executable(main main.cpp ${SRC})
#4、连接动态库
target_link_libraries(main pthread)