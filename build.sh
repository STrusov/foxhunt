#!/bin/sh

DEBUG=1 DEFINES=-DVK_DETAILED_LOG make clean all
mv foxhunt foxhunt_dbg
CC=clang make clean all
strip foxhunt
