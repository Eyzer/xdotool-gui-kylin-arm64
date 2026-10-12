QT += core gui widgets

TARGET = xdotool-gui
TEMPLATE = app
CONFIG += release c++11
CONFIG -= debug_and_release debug

SOURCES += main.cpp

XDOTOOL_SRC = /build/xdotool_src
INCLUDEPATH += $$XDOTOOL_SRC

LIBS += $$XDOTOOL_SRC/libxdo.a -lX11 -lXtst -lXinerama -lxkbcommon -lm

QMAKE_CXXFLAGS += -O2 -fno-strict-aliasing
