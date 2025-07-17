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

# Create the executable
add_executable(BigPictureSwitch WIN32
    BigPictureSwitch.cpp
    BigPictureSwitch.h
    framework.h
    resource.h
    targetver.h
    BigPictureSwitch.rc
    icon.ico
)

target_compile_features(BigPictureSwitch PRIVATE cxx_std_20)


# Set Windows subsystem
set_target_properties(BigPictureSwitch PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "BigPictureSwitch"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
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
    advapi32
    shell32
    uuid
    odbc32
    odbccp32
    winmm
    setupapi
    cfgmgr32
    shlwapi
)

# Compiler definitions
target_compile_definitions(BigPictureSwitch PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00  # Windows 10
    WINVER=0x0A00
)

# Compiler options
if(MSVC)
    target_compile_options(BigPictureSwitch PRIVATE
        /W4
        /WX-  # Don't treat warnings as errors
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )
    
    # Debug/Release specific options
    target_compile_options(BigPictureSwitch PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
        $<$<CONFIG:Release>:/O2 /GL>
    )
    
    # Debug/Release specific link options
    target_link_options(BigPictureSwitch PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()