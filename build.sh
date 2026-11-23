#!/bin/sh
# Сборка под Windows из Linux (нужен mingw-w64): ./build.sh
set -e
x86_64-w64-mingw32-windres -c 65001 app.rc -O coff -o app.res.o
F="-O2 -s -municode -mwindows -static -Wall"
L="-lgdiplus -lgdi32 -luser32 -lshell32 -lole32 -loleaut32 -ladvapi32 -lwinmm -luuid -lws2_32 -lcomdlg32 -lversion -lcomctl32 -lpowrprof -lurlmon -lwininet -ldwmapi"
x86_64-w64-mingw32-g++ $F -o "Напоминалка.exe" stand_reminder.cpp app.res.o $L
x86_64-w64-mingw32-g++ $F -DTEST_MODE -o "Напоминалка_тест_1мин.exe" stand_reminder.cpp app.res.o $L
