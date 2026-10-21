# The evbot MicroPython module. The EVBot engine is compiled straight from
# the C++ library's src/ folder (never copied), on top of the Arduino shim.
# Wireless.cpp is left out: wireless_mp.cpp replaces it.

get_filename_component(EVBOT_REPO ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
set(EVBOT_SRC ${EVBOT_REPO}/src)
set(EVBOT_SHIM ${CMAKE_CURRENT_LIST_DIR}/../arduino_shim)

add_library(usermod_evbot INTERFACE)

target_sources(usermod_evbot INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modevbot.c
    ${CMAKE_CURRENT_LIST_DIR}/evbot_glue.cpp
    ${CMAKE_CURRENT_LIST_DIR}/wireless_mp.cpp
    ${EVBOT_SHIM}/Arduino.cpp
    ${EVBOT_SHIM}/Wire.cpp
    ${EVBOT_SHIM}/WString.cpp
    ${EVBOT_SRC}/EVBot.cpp
    ${EVBOT_SRC}/internal/Board.cpp
    ${EVBOT_SRC}/internal/Encoders.cpp
    ${EVBOT_SRC}/internal/Engine.cpp
    ${EVBOT_SRC}/internal/Gyro.cpp
    ${EVBOT_SRC}/internal/Motion.cpp
    ${EVBOT_SRC}/internal/Steppers.cpp
)

target_include_directories(usermod_evbot INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    ${EVBOT_SHIM}
    ${EVBOT_SRC}
    ${EVBOT_SRC}/internal
)

target_link_libraries(usermod INTERFACE usermod_evbot)
