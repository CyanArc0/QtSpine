# include(disable-Exceptions.cmake)

message(STATUS "Exceptions disabled")

if(MSVC)
    add_compile_options(/EHs- /EHc-)
    add_definitions(-D_HAS_EXCEPTIONS=0)
else()# GCC & Clang
    add_compile_options(-fno-exceptions)
endif()

add_definitions(-DBOOST_NO_EXCEPTIONS)