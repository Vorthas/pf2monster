# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas

cmake_minimum_required(VERSION 3.21)
project(pf2monster VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

add_executable(pf2monster
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/Model/Adjustments.cpp
    src/Model/Adjustments.h
    src/Model/Benchmarks.cpp
    src/Model/Benchmarks.h
    src/Model/Homebrew.cpp
    src/Model/Homebrew.h
    src/Model/Monster.cpp
    src/Model/Monster.h
    src/Model/Mythic.cpp
    src/Model/Mythic.h
    src/Model/RecallKnowledge.cpp
    src/Model/RecallKnowledge.h
    src/Model/Roadmaps.cpp
    src/Model/Roadmaps.h
    src/Model/ScribeExport.cpp
    src/Model/ScribeExport.h
    src/Model/TraitNotes.cpp
    src/Model/TraitNotes.h
    src/Rendering/Fonts.cpp
    src/Rendering/Fonts.h
    src/Rendering/ImageCache.cpp
    src/Rendering/ImageCache.h
    src/Rendering/PageLayout.cpp
    src/Rendering/PageLayout.h
    src/Rendering/ScribeParser.cpp
    src/Rendering/ScribeParser.h
    src/Rendering/Theme.cpp
    src/Rendering/Statblock.cpp
    src/Rendering/Statblock.h
    src/Rendering/Theme.h
    src/Rendering/TraitBadge.cpp
    src/Rendering/TraitBadge.h
    src/Widgets/AdjustmentEditor.cpp
    src/Widgets/AdjustmentEditor.h
    src/Widgets/EntryList.cpp
    src/Widgets/EntryList.h
    src/Widgets/HomebrewEditor.cpp
    src/Widgets/HomebrewEditor.h
    src/Widgets/HomebrewEditors.cpp
    src/Widgets/HomebrewEditors.h
    src/Widgets/HomebrewFoldersDialog.cpp
    src/Widgets/HomebrewFoldersDialog.h
    src/Widgets/MonsterEditor.cpp
    src/Widgets/MonsterList.cpp
    src/Widgets/MonsterList.h
    src/Widgets/MonsterEditor.h
    src/Widgets/MythicDialog.cpp
    src/Widgets/MythicDialog.h
    src/Widgets/OptionsDialog.cpp
    src/Widgets/OptionsDialog.h
    src/Widgets/PreviewWidget.cpp
    src/Widgets/PreviewWidget.h
)

# Includes are written relative to src/ ("Model/Monster.h").
target_include_directories(pf2monster PRIVATE src)
target_link_libraries(pf2monster PRIVATE Qt6::Widgets)
target_compile_definitions(pf2monster PRIVATE PF2MONSTER_VERSION="${PROJECT_VERSION}")

# Fontconfig (Linux): to fix the metadata of installed fonts as well, see
# fixInstalledFonts() in src/Rendering/Fonts.cpp.
find_package(Fontconfig)
if(Fontconfig_FOUND)
    target_link_libraries(pf2monster PRIVATE Fontconfig::Fontconfig)
    target_compile_definitions(pf2monster PRIVATE SCRIBE_HAVE_FONTCONFIG)
endif()

# Fonts baked into the executable (available at runtime as :/fonts/...).
# By default only fonts/free (free fonts, shipped with their licenses), so a
# build can be shared. PF2MONSTER_EMBED_ALL_FONTS=ON embeds everything in
# fonts/, commercial fonts included: for personal builds only, never to be
# shared. Either way, the app also loads fonts/ from beside the executable or
# the current directory when it starts. Re-run cmake after adding or removing
# font files.
option(PF2MONSTER_EMBED_ALL_FONTS
       "Embed every font in fonts/, commercial ones too (personal builds only)" OFF)
if(PF2MONSTER_EMBED_ALL_FONTS)
    set(PF2MONSTER_FONT_DIR "${CMAKE_SOURCE_DIR}/fonts")
else()
    set(PF2MONSTER_FONT_DIR "${CMAKE_SOURCE_DIR}/fonts/free")
endif()
file(GLOB_RECURSE PF2MONSTER_FONT_FILES CONFIGURE_DEPENDS
    "${PF2MONSTER_FONT_DIR}/*.ttf" "${PF2MONSTER_FONT_DIR}/*.otf")
# fonts/Unused holds fonts set aside (the app doesn't load them either).
list(FILTER PF2MONSTER_FONT_FILES EXCLUDE REGEX "/[Uu]nused/")
if(PF2MONSTER_FONT_FILES)
    list(LENGTH PF2MONSTER_FONT_FILES PF2MONSTER_FONT_COUNT)
    if(PF2MONSTER_EMBED_ALL_FONTS)
        message(STATUS "Embedding all ${PF2MONSTER_FONT_COUNT} fonts in fonts/, "
                       "commercial ones included: don't share this build")
    else()
        message(STATUS "Embedding the ${PF2MONSTER_FONT_COUNT} free fonts in fonts/free "
                       "(-DPF2MONSTER_EMBED_ALL_FONTS=ON for all of fonts/)")
    endif()
    # Paths stay relative to fonts/, so :/fonts/free/... with the default.
    qt_add_resources(pf2monster "pf2monster_fonts"
        PREFIX "/fonts"
        BASE "${CMAKE_SOURCE_DIR}/fonts"
        FILES ${PF2MONSTER_FONT_FILES})
endif()

# Rules data (GM Core's creature-building tables and trait abilities, see
# tools/scripts/aon_benchmarks.py and aon_traits.py; the road maps,
# adjustments and templates, one file each), available at runtime as
# :/data/...
file(GLOB PF2MONSTER_RULES_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/roadmaps/*.json"
    "${CMAKE_SOURCE_DIR}/resources/adjustments/*.json")
qt_add_resources(pf2monster "pf2monster_data"
    PREFIX "/data"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${CMAKE_SOURCE_DIR}/resources/benchmarks.json" "${CMAKE_SOURCE_DIR}/resources/dcs.json"
          "${CMAKE_SOURCE_DIR}/resources/traits.json" "${CMAKE_SOURCE_DIR}/resources/domains.json"
          "${CMAKE_SOURCE_DIR}/resources/recall-knowledge.json" "${CMAKE_SOURCE_DIR}/resources/ancestry-traits.json"
          "${CMAKE_SOURCE_DIR}/resources/credits.html" ${PF2MONSTER_RULES_FILES})

if(MSVC)
    target_compile_options(pf2monster PRIVATE /W4)
else()
    target_compile_options(pf2monster PRIVATE -Wall -Wextra)
endif()
