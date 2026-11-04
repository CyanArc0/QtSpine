# include(disable-RTTI.cmake)

message(STATUS "RTTI disabled")

if(MSVC)
    add_compile_options(/GR-)
else()# GCC & Clang
    add_compile_options(-fno-rtti)
endif()

add_definitions(-DBOOST_NO_RTTI)