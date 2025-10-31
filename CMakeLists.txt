cmake_minimum_required(VERSION 2.5)
project(mymuduo)

# mymuduo最终被编译成so动态库，设置动态库的路径，放在根目录下面的lib文件夹下面
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
file(MAKE_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
# 设置调试信息
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# 定义参与编译的源代码文件
aux_source_directory(. SRC_LIST)
# 编译动态库
add_library(mymuduo SHARED ${SRC_LIST})