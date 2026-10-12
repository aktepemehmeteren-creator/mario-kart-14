cmake_minimum_required(VERSION 3.13)
project(MarioKartPSP C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(MarioKartPSP main.c)

target_compile_options(MarioKartPSP PRIVATE
    -O2 -G0 -Wall -Wextra
    -fno-strict-aliasing
    -fsingle-precision-constant
)

target_link_options(MarioKartPSP PRIVATE
    -Wl,-zmax-page-size=128
)

target_link_libraries(MarioKartPSP PRIVATE
    pspgum
    pspgu
    pspge
    pspdisplay
    pspctrl
    pspaudio
    psppower
    m
)

create_pbp_file(
    TARGET MarioKartPSP
    TITLE "Mario Kart PSP"
    VERSION 01.00
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
)
