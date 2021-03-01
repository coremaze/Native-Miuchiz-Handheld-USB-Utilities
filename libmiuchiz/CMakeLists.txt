cmake_minimum_required(VERSION 3.0.0)
project(miuchiz VERSION 0.1.0)
include_directories(include)
add_library(miuchiz src/libmiuchiz.c
                    src/commands.c
                    src/timer.c)