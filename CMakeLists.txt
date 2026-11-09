cmake_minimum_required(VERSION 3.16)

# Version annoncée à Cornelius dans « hello » (32 caractères au plus).
set(PROJECT_VER "0.5.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cornelius-satellite)
