QT += core gui qml quick quickcontrols2 dbus svg

CONFIG += c++20 release
TARGET = dilute
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/calc.h \
    src/card.h \
    src/cli.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/recipefile.h \
    src/recipemodel.h \
    src/spacekey.h \
    src/theme.h \
    src/translator.h \
    src/units.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/calc.cpp \
    src/card.cpp \
    src/cli.cpp \
    src/portalfilepicker.cpp \
    src/recipefile.cpp \
    src/recipemodel.cpp \
    src/spacekey.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/units.cpp

RESOURCES += src/resources.qrc
