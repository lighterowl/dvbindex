cmake_minimum_required(VERSION 3.1)

project(dvbindex C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig)
pkg_check_modules(FFMPEG REQUIRED libavformat libavutil libavcodec)
pkg_check_modules(DVBPSI REQUIRED libdvbpsi)
pkg_check_modules(SQLITE REQUIRED sqlite3)

add_executable(${PROJECT_NAME}
  main.c
  vec.h
  export.c
  export.h
  util.h
  read.c
  read.h
  dvbstring.c
  dvbstring.h
)

target_link_libraries(${PROJECT_NAME}
  ${FFMPEG_LIBRARIES}
  ${DVBPSI_LIBRARIES}
  ${SQLITE_LIBRARIES}
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${FFMPEG_INCLUDE_DIRS}
  ${DVBPSI_INCLUDE_DIRS}
  ${SQLITE_INCLUDE_DIRS}
)

target_compile_options(${PROJECT_NAME}
  PUBLIC
  ${FFMPEG_CFLAGS_OTHER}
  ${DVBPSI_CFLAGS_OTHER}
  ${SQLITE_CFLAGS_OTHER}
)

target_compile_definitions(${PROJECT_NAME}
  PUBLIC
  _FILE_OFFSET_BITS=64
)
