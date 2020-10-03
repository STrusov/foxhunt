#!/bin/sh

# X11 с отладкой.
DEBUG=1 DEFINES=-DFH_VK_DETAILED_LOG make X11=1 clean all
mv foxhunt foxhunt_xcb_dbg

# X11
CC=clang make X11=1 clean all
strip foxhunt
mv foxhunt foxhunt_xcb

# Wayland с отладкой.
DEBUG=1 DEFINES=-DFH_VK_DETAILED_LOG make clean all
mv foxhunt foxhunt_dbg

# Wayland
CC=clang make clean all
strip foxhunt
