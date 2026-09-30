# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.19)
project(AiBoosterLinux VERSION 2.2.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "This repository is the Linux client. Building it elsewhere is not supported — the "
        "engine supervision and the desktop-proxy integration are Linux-specific.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

# 6.2, not 6.4: Ubuntu 22.04 LTS ships Qt 6.2 and is supported until 2027. Verified
# building and running there — see README.
find_package(Qt6 6.2 REQUIRED COMPONENTS Quick QuickControls2 Network Widgets)
# qt_standard_project_setup() only exists from Qt 6.3.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# ── Deployment configuration ─────────────────────────────────────────────────────────
# The free nodes come from ONE operator's service. Its endpoints are deployment
# configuration, not client behaviour, and are deliberately absent from the source: a fork
# points them at its own service, or leaves them out and uses only the profiles its users
# import.
#
# A default build compiles and runs; it just cannot fetch free nodes until these are
# supplied, and it says so plainly instead of failing obscurely.
set(AIBOOSTER_FREE_URL "https://example.invalid/?type=free"
    CACHE STRING "Primary free profile endpoint")
set(AIBOOSTER_FREE_BACKUP_URL "https://example.invalid/?type=free"
    CACHE STRING "Backup free profile endpoint")
if(AIBOOSTER_FREE_URL MATCHES "\\.invalid([/:?]|$)"
   AND AIBOOSTER_FREE_BACKUP_URL MATCHES "\\.invalid([/:?]|$)")
    message(STATUS "AIBOOSTER_FREE_URL is not set: this build cannot fetch free nodes (profiles "
                   "users import still work). See README.md -> Deployment configuration.")
endif()

qt_add_executable(aibooster
    src/main.cpp
    src/core/CoreProcess.cpp
    src/core/CoreProcess.h
    src/core/PrivateFiles.cpp
    src/core/PrivateFiles.h
    src/platform/SystemProxy.cpp
    src/platform/SystemProxy.h
    src/platform/TrayIcon.cpp
    src/platform/TrayIcon.h
    src/models/connectionmodel.cpp
    src/models/logsmodel.cpp
    src/models/profilelistmodel.cpp
    src/models/proxylistmodel.cpp
    src/models/settingsmodel.cpp
    src/models/vpncore.cpp
    src/services/ClashApi.cpp
    src/services/FreeProfiles.cpp
    src/services/SubscriptionParser.cpp
    src/services/SubscriptionParser.h
)

qt_add_qml_module(aibooster
    URI AiBooster
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/SpaceBackground.qml
        qml/components/ConnectionButton.qml
        qml/components/ActiveProxyCard.qml
        qml/components/ProxyTile.qml
        qml/components/ProfileTile.qml
        qml/components/StatusBadge.qml
        qml/screens/HomeScreen.qml
        qml/screens/ProxiesScreen.qml
        qml/screens/ProfilesScreen.qml
        qml/screens/SettingsScreen.qml
        qml/screens/LogsScreen.qml
        qml/screens/AboutScreen.qml
        qml/screens/LicensesScreen.qml
        qml/screens/settings/RouteSettings.qml
        qml/screens/settings/DnsSettings.qml
        qml/screens/settings/InboundSettings.qml
        qml/screens/settings/TlsTricks.qml
        qml/screens/settings/WarpSettings.qml
    RESOURCES
        packaging/aibooster.svg
        qml/icons/home.svg
        qml/icons/proxies.svg
        qml/icons/profiles.svg
        qml/icons/settings.svg
        qml/icons/logs.svg
        qml/icons/about.svg
        qml/icons/shield.svg
        qml/icons/shield-check.svg
        qml/icons/tray-off.svg
        qml/icons/tray-connecting.svg
        qml/icons/tray-on.svg
)

# ── Translations ─────────────────────────────────────────────────────────────────────
# English is the source language. Each i18n/aibooster_<lang>.ts is compiled to a .qm and
# embedded under :/i18n, where main.cpp's applyLanguage() looks. Linguist is optional: a
# build without it is English-only rather than failing.
#
# After changing user-visible strings, refresh the .ts files (then translate the new
# entries, e.g. in Qt Linguist):
#   lupdate -locations none -no-obsolete -source-language en src qml \
#       -ts i18n/aibooster_zh_CN.ts
option(AIBOOSTER_REQUIRE_TRANSLATIONS
       "Fail instead of building English-only when Linguist is missing (release builds)" OFF)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    qt_add_translations(aibooster TS_FILES i18n/aibooster_zh_CN.ts)
elseif(AIBOOSTER_REQUIRE_TRANSLATIONS)
    message(FATAL_ERROR "Qt6 LinguistTools not found, and AIBOOSTER_REQUIRE_TRANSLATIONS is on. "
                        "Install qt6-tools-dev and qt6-l10n-tools.")
else()
    message(WARNING "Qt6 LinguistTools not found: building without translations (English "
                    "only). Install qt6-tools-dev and qt6-l10n-tools to include them.")
endif()

target_compile_definitions(aibooster PRIVATE
    AIBOOSTER_VERSION="${PROJECT_VERSION}"
    AIBOOSTER_FREE_URL="${AIBOOSTER_FREE_URL}"
    AIBOOSTER_FREE_BACKUP_URL="${AIBOOSTER_FREE_BACKUP_URL}"
)

target_link_libraries(aibooster PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
)

# ── Tests ────────────────────────────────────────────────────────────────────────────
# Exercise the two subsystems that cannot be checked by compiling: engine supervision
# (against a stub engine, so no GPL engine binary is needed) and the desktop-proxy round
# trip (which writes machine-wide state, so the test that matters is that it is restored).
# Plus the parsers that decide what reaches the engine: deep links, engine settings and
# the free-node index.
option(AIBOOSTER_BUILD_TESTS "Build the test suite" ON)

if(AIBOOSTER_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    qt_add_executable(test_coreprocess
        tests/test_coreprocess.cpp
        src/core/CoreProcess.cpp
        src/core/CoreProcess.h
    )
    target_link_libraries(test_coreprocess PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME coreprocess COMMAND test_coreprocess)

    # SettingsModel pulls in QML registration macros, so the test links Quick.
    qt_add_executable(test_enginesettings
        tests/test_enginesettings.cpp
        src/models/settingsmodel.cpp
        src/models/settingsmodel.h
    )
    target_link_libraries(test_enginesettings PRIVATE Qt6::Test Qt6::Quick)
    add_test(NAME enginesettings COMMAND test_enginesettings)

    qt_add_executable(test_deeplinks
        tests/test_deeplinks.cpp
        src/models/profilelistmodel.cpp
        src/models/profilelistmodel.h
        src/core/PrivateFiles.cpp
        src/services/SubscriptionParser.cpp
    )
    target_link_libraries(test_deeplinks PRIVATE Qt6::Test Qt6::Quick Qt6::Network)
    add_test(NAME deeplinks COMMAND test_deeplinks)

    qt_add_executable(test_systemproxy
        tests/test_systemproxy.cpp
        src/platform/SystemProxy.cpp
        src/platform/SystemProxy.h
        src/core/PrivateFiles.cpp
    )
    target_link_libraries(test_systemproxy PRIVATE Qt6::Test)
    add_test(NAME systemproxy COMMAND test_systemproxy)

    # Against a loopback HTTP server, never the real endpoints — so the cache variables are
    # compiled in only because the source needs them, and the result does not depend on them.
    qt_add_executable(test_freeprofiles
        tests/test_freeprofiles.cpp
        src/services/FreeProfiles.cpp
        src/services/FreeProfiles.h
    )
    target_compile_definitions(test_freeprofiles PRIVATE
        AIBOOSTER_FREE_URL="${AIBOOSTER_FREE_URL}"
        AIBOOSTER_FREE_BACKUP_URL="${AIBOOSTER_FREE_BACKUP_URL}"
    )
    target_link_libraries(test_freeprofiles PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME freeprofiles COMMAND test_freeprofiles)

    qt_add_executable(test_privatefiles
        tests/test_privatefiles.cpp
        src/core/PrivateFiles.cpp
    )
    target_link_libraries(test_privatefiles PRIVATE Qt6::Test)
    add_test(NAME privatefiles COMMAND test_privatefiles)

    qt_add_executable(test_logsmodel
        tests/test_logsmodel.cpp
        src/models/logsmodel.cpp
        src/models/logsmodel.h
    )
    target_link_libraries(test_logsmodel PRIVATE Qt6::Test Qt6::Quick)
    add_test(NAME logsmodel COMMAND test_logsmodel)

    # Against a stub engine; links the pieces VpnCore drives (engine supervision, the
    # desktop proxy, the control API client) but never the real engine.
    qt_add_executable(test_vpncore
        tests/test_vpncore.cpp
        src/models/vpncore.cpp
        src/models/vpncore.h
        src/models/settingsmodel.cpp
        src/core/CoreProcess.cpp
        src/core/PrivateFiles.cpp
        src/platform/SystemProxy.cpp
        src/services/ClashApi.cpp
        src/services/SubscriptionParser.cpp
    )
    target_link_libraries(test_vpncore PRIVATE Qt6::Test Qt6::Quick Qt6::Network)
    add_test(NAME vpncore COMMAND test_vpncore)

    # The whole connection pipeline minus the UI, against a stub engine.
    qt_add_executable(test_connectionmodel
        tests/test_connectionmodel.cpp
        src/models/connectionmodel.cpp
        src/models/profilelistmodel.cpp
        src/models/proxylistmodel.cpp
        src/models/settingsmodel.cpp
        src/models/vpncore.cpp
        src/core/CoreProcess.cpp
        src/core/PrivateFiles.cpp
        src/platform/SystemProxy.cpp
        src/services/ClashApi.cpp
        src/services/FreeProfiles.cpp
        src/services/SubscriptionParser.cpp
    )
    target_compile_definitions(test_connectionmodel PRIVATE
        AIBOOSTER_FREE_URL="${AIBOOSTER_FREE_URL}"
        AIBOOSTER_FREE_BACKUP_URL="${AIBOOSTER_FREE_BACKUP_URL}"
    )
    target_link_libraries(test_connectionmodel PRIVATE Qt6::Test Qt6::Quick Qt6::Network)
    add_test(NAME connectionmodel COMMAND test_connectionmodel)

    qt_add_executable(test_subscriptionparser
        tests/test_subscriptionparser.cpp
        src/services/SubscriptionParser.cpp
    )
    target_link_libraries(test_subscriptionparser PRIVATE Qt6::Test)
    add_test(NAME subscriptionparser COMMAND test_subscriptionparser)
endif()

# ── Install (XDG layout) ─────────────────────────────────────────────────────────────
install(TARGETS aibooster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.allianceinterstellar.AiBooster.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES packaging/aibooster.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME io.allianceinterstellar.AiBooster.svg)
install(FILES packaging/io.allianceinterstellar.AiBooster.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
