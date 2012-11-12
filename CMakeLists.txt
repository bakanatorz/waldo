CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

PROJECT(sdj C)

INCLUDE_DIRECTORIES(src)

ADD_DEFINITIONS(-g -Wall -std=c99)

ADD_EXECUTABLE(waldo_play play.c libao.c)
TARGET_LINK_LIBRARIES(waldo_play despotify ao)

ADD_EXECUTABLE(waldo_wave wave.c)
TARGET_LINK_LIBRARIES(waldo_wave despotify)

ADD_EXECUTABLE(waldo_ogg ogg.c sndqueue.c)
TARGET_LINK_LIBRARIES(waldo_ogg despotify vorbisfile)

ADD_EXECUTABLE(waldo_server server.c mongoose/mongoose.c sndqueue.c)
TARGET_LINK_LIBRARIES(waldo_server despotify vorbisfile)
