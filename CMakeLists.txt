cmake_minimum_required(VERSION 3.31)
project(BigPictureSwitch 
    VERSION 1.1.0 
    LANGUAGES CXX
    DESCRIPTION "Big Picture Switch application for Windows"
)

# Platform check
if(NOT WIN32)
    message(FATAL_ERROR "This project only supports Windows")
endif()

include(ExternalProject)
ExternalProject_Add(platform
  SOURCE_DIR    ${CMAKE_SOURCE_DIR}/platform
  #BINARY_DIR    ${CMAKE_CURRENT_BINARY_DIR}/platform-build
  #INSTALL_DIR   ${CMAKE_CURRENT_BINARY_DIR}/vendor
  BUILD_BYPRODUCTS
    <INSTALL_DIR>/lib/p8-platform.lib
  BUILD_COMMAND
    ${CMAKE_COMMAND}
      --build <BINARY_DIR>
      --config $<CONFIG>       # Debug, Release, RelWithDebInfo, etc.
  INSTALL_COMMAND     ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
  CMAKE_ARGS    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=$<CONFIG>
)

# 2) Grab where platform ended up installing
ExternalProject_Get_Property(platform INSTALL_DIR)

list (APPEND CMAKE_PREFIX_PATH "${INSTALL_DIR}")

ExternalProject_Add(libcec
  SOURCE_DIR    ${CMAKE_SOURCE_DIR}/libcec
  #BINARY_DIR    ${CMAKE_CURRENT_BINARY_DIR}/libcec-build
  #INSTALL_DIR   ${CMAKE_CURRENT_BINARY_DIR}/vendor
  DEPENDS       platform
  BUILD_COMMAND
    ${CMAKE_COMMAND}
      --build <BINARY_DIR>
      --config $<CONFIG>       # Debug, Release, RelWithDebInfo, etc.
  BUILD_BYPRODUCTS
    <INSTALL_DIR>/cec-static.lib
  INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG>
  CMAKE_ARGS -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>  -DCMAKE_BUILD_TYPE=$<CONFIG> -DSKIP_PYTHON_WRAPPER:bool=true -DLIBCEC_VERSION_MAJOR:string=7 -DLIBCEC_VERSION_MINOR:string=1 -DLIBCEC_VERSION_PATCH:string=1
)

# Create the executable
add_executable(BigPictureSwitch WIN32
    BigPictureSwitch.cpp
    BigPictureSwitchCEC.cpp
    BigPictureSwitch.rc
    icon.ico "BigPictureSwitchAudio.cpp" "BigPictureSwitchAudio.h" "BigPictureSwitchDisplay.cpp" "BigPictureSwitchDisplay.h")

target_compile_features(BigPictureSwitch PRIVATE cxx_std_20)

# Set Windows subsystem
set_target_properties(BigPictureSwitch PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "BigPictureSwitch"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR};${CMAKE_SOURCE_DIR}/libcec/include;${CMAKE_BINARY_DIR}/vendor/include"
)

add_dependencies(BigPictureSwitch libcec)

# 2) Grab where libcec ended up installing
ExternalProject_Get_Property(libcec INSTALL_DIR)

# 3) Create an IMPORTED target for the static library
add_library(cec-static STATIC IMPORTED)
set_target_properties(cec-static PROPERTIES
  IMPORTED_LOCATION
    "${INSTALL_DIR}/cec-static.lib"
)

# 2) Grab where platform ended up installing
ExternalProject_Get_Property(platform INSTALL_DIR)

# 3) Create an IMPORTED target for the static library
add_library(p8-platform STATIC IMPORTED)
set_target_properties(p8-platform PROPERTIES
  IMPORTED_LOCATION
    "${INSTALL_DIR}/lib/p8-platform.lib"
)

# Link Windows libraries
target_link_libraries(BigPictureSwitch PRIVATE
    ole32
    oleaut32
    user32
    kernel32
    gdi32
    winspool
    comdlg32
    comctl32
    advapi32
    shell32
    uuid
    odbc32
    odbccp32
    winmm
    setupapi
    cfgmgr32
    shlwapi
    gameinput
    cec-static
    p8-platform
)

# Compiler definitions
target_compile_definitions(BigPictureSwitch PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

# Compiler options
if(MSVC)
    target_compile_options(BigPictureSwitch PRIVATE
        /W4
        /WX-  # Don't treat warnings as errors
        /permissive-
        /Zc:__cplusplus
        /EHsc
        /utf-8
    )
    
    # Debug/Release specific options
    target_compile_options(BigPictureSwitch PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
        $<$<CONFIG:Release>:/O2 /GL /Gy>
    )
    
    # Debug/Release specific link options
    target_link_options(BigPictureSwitch PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )

    cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Release,RelWithDebInfo>:ProgramDatabase>")
endif()