cmake_minimum_required(VERSION 3.2)

project(ohPlayer)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3")

set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR})

set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET libavresample)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET libavformat)

set(TARG_ARCH Linux-x64)

file(GLOB SRCS linux/*.cpp)
add_executable(${PROJECT_NAME} ${SRCS})
target_link_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohMediaPlayer/lib 
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNetmon/lib 
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/openssl/lib
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNet-${TARG_ARCH}-${CMAKE_BUILD_TYPE}/lib 
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNetGenerated-${TARG_ARCH}-${CMAKE_BUILD_TYPE}/lib
  )
target_link_libraries(${PROJECT_NAME}
  PkgConfig::deps
  CodecAacFdkAdts CodecAacFdk CodecAacFdkBase CodecMp3 CodecAacFdkMp4
  asound ConfigUiTestUtils ConfigUi SourcePlaylist Podcast SourceSongcast SourceUpnpAv SourceRadio ohMediaPlayer WebAppFramework ohNetGeneratedProxies ohNetCore CodecAifc CodecAlacApple CodecAlacAppleBase CodecPcm CodecAiff CodecAiffBase CodecVorbis libOgg CodecFlac CodecWav ohPipeline pthread ssl crypto dl m
  )
target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohMediaPlayer/include
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNetmon/include
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/openssl/include
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNetGenerated-${TARG_ARCH}-${CMAKE_BUILD_TYPE}/include/ohnet/OpenHome/Net/Core
  ${CMAKE_SOURCE_DIR}/dependencies/${TARG_ARCH}/ohNet-${TARG_ARCH}-${CMAKE_BUILD_TYPE}/include/ohnet
  )
target_compile_definitions(${PROJECT_NAME} PUBLIC DEFINE_LITTLE_ENDIAN)
