cmake_minimum_required(VERSION 3.16)
# Build only the companion and its declared dependencies. The default IDF
# project compiles Wi-Fi and other unused subsystems for this touch/button game.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(diablito)
