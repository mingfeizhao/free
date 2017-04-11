#指定源文件
include_directories(${CMAKE_SOURCE_DIR}/src)
aux_source_directory(. MAIN_SRCS)

#生成目标
add_executable(FREE ${MAIN_SRCS})
target_link_libraries(FREE libhiredis.a libcjson.a libev.a m)




