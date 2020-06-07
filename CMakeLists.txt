cmake_minimum_required(VERSION 3.10)
include_directories(include)
link_directories(lib/x86_64)
add_compile_options(-W -Wall -Wchar-subscripts -Wcomment -Wformat -Wimplicit)
add_compile_options( -Wmissing-braces -Wparentheses -Wswitch -Wunused -Wunused-parameter  -Wfloat-equal)
add_compile_options( -Wsign-compare -Wpointer-arith -Winline -Werror -g) 

add_subdirectory(cli)
add_subdirectory(libubox)
                              
