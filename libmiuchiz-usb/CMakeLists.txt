cmake_minimum_required(VERSION 3.0.0)
project(miuchiz-usb VERSION 0.1.0)
include_directories(include)
add_library(miuchiz-usb src/libmiuchiz-usb.c
                        src/commands.c
                        src/timer.c)