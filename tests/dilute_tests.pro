QT += core gui qml quick quickcontrols2 testlib dbus svg
CONFIG += c++20 testcase
TARGET = dilute_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += SRCDIR=\\\"$$PWD/../src\\\"

HEADERS += \
    ../src/backend.h \
    ../src/calc.h \
    ../src/card.h \
    ../src/cli.h \
    ../src/filepicker.h \
    ../src/recipefile.h \
    ../src/recipemodel.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/units.h

SOURCES += \
    dilute_tests.cpp \
    ../src/backend.cpp \
    ../src/calc.cpp \
    ../src/card.cpp \
    ../src/cli.cpp \
    ../src/recipefile.cpp \
    ../src/recipemodel.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/units.cpp

RESOURCES += ../src/resources.qrc
