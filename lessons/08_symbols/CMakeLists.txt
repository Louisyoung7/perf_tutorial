# 同一份源码，三种构建方式，只改调试信息与符号表
add_executable(08_symbols main.cpp)
target_compile_definitions(08_symbols PRIVATE LESSON08_VARIANT="full")

# 对照组一：有符号表，没有调试信息
add_executable(08_symbols_nodebug main.cpp)
target_compile_options(08_symbols_nodebug PRIVATE -g0)
target_compile_definitions(08_symbols_nodebug PRIVATE LESSON08_VARIANT="nodebug")

# 对照组二：连符号表一起剥掉
add_executable(08_symbols_stripped main.cpp)
target_compile_definitions(08_symbols_stripped PRIVATE LESSON08_VARIANT="stripped")
add_custom_command(TARGET 08_symbols_stripped POST_BUILD
  COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:08_symbols_stripped>)
