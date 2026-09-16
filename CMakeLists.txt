# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.19)
project(AiBoosterLinux VERSION 2.1.0 LANGUAGES CXX)

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
find_package(Qt6 6.2 REQUIRED COMPONENTS Quick QuickControls2 Network)
# qt_standard_project_setup() only exists from Qt 6.3.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# The subscription payload is AES-256-GCM; we decrypt it with OpenSSL's EVP interface.
find_package(OpenSSL REQUIRED)

# ── Deployment configuration ─────────────────────────────────────────────────────────
# This client is the user interface for a subscription service. The endpoints and the
# payload key below identify ONE operator and are deliberately absent from the source:
# publishing the key would hand that operator's paid subscription data to everyone.
#
# A default build compiles and runs; it just cannot talk to anybody's service until these
# are supplied, and it says so plainly instead of failing obscurely.
set(AIBOOSTER_PREMIUM_AES_KEY_B64 "REPLACE_WITH_YOUR_BASE64_AES_256_KEY"
    CACHE STRING "Base64 AES-256 key for the encrypted subscription payload")
set(AIBOOSTER_PREMIUM_URL "https://example.invalid/?iso=1"
    CACHE STRING "Primary premium subscription endpoint")
set(AIBOOSTER_PREMIUM_BACKUP_URL "https://example.invalid/?iso=1"
    CACHE STRING "Backup premium subscription endpoint")
set(AIBOOSTER_FREE_URL "https://example.invalid/?type=free"
    CACHE STRING "Primary free profile endpoint")
set(AIBOOSTER_FREE_BACKUP_URL "https://example.invalid/?type=free"
    CACHE STRING "Backup free profile endpoint")
set(AIBOOSTER_API_BASE_URL "https://example.invalid"
    CACHE STRING "Account/subscription API base URL")
# Firebase Web API keys are public identifiers, not credentials — access is governed by
# Firebase security rules. Parameterised so a fork points at its own project.
set(AIBOOSTER_FIREBASE_API_KEY "REPLACE_WITH_YOUR_FIREBASE_WEB_API_KEY"
    CACHE STRING "Firebase Web API key")

qt_add_executable(aibooster
    src/main.cpp
    src/core/CoreProcess.cpp
    src/core/CoreProcess.h
    src/platform/SystemProxy.cpp
    src/platform/SystemProxy.h
    src/models/connectionmodel.cpp
    src/models/logsmodel.cpp
    src/models/profilelistmodel.cpp
    src/models/proxylistmodel.cpp
    src/models/settingsmodel.cpp
    src/models/vpncore.cpp
    src/services/AccountManager.cpp
    src/services/ApiClient.cpp
    src/services/ClashApi.cpp
    src/services/FirebaseAuth.cpp
    src/services/PremiumConfig.cpp
)

qt_add_qml_module(aibooster
    URI AiBooster
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/SpaceBackground.qml
        qml/components/ConnectionButton.qml
        qml/components/LoginDialog.qml
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
        qml/icons/crown.svg
)

target_compile_definitions(aibooster PRIVATE
    AIBOOSTER_VERSION="${PROJECT_VERSION}"
    AIBOOSTER_PREMIUM_AES_KEY_B64="${AIBOOSTER_PREMIUM_AES_KEY_B64}"
    AIBOOSTER_PREMIUM_URL="${AIBOOSTER_PREMIUM_URL}"
    AIBOOSTER_PREMIUM_BACKUP_URL="${AIBOOSTER_PREMIUM_BACKUP_URL}"
    AIBOOSTER_FREE_URL="${AIBOOSTER_FREE_URL}"
    AIBOOSTER_FREE_BACKUP_URL="${AIBOOSTER_FREE_BACKUP_URL}"
    AIBOOSTER_API_BASE_URL="${AIBOOSTER_API_BASE_URL}"
    AIBOOSTER_FIREBASE_API_KEY="${AIBOOSTER_FIREBASE_API_KEY}"
)

target_link_libraries(aibooster PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    OpenSSL::Crypto
)

# ── Tests ────────────────────────────────────────────────────────────────────────────
# Exercise the two subsystems that cannot be checked by compiling: engine supervision
# (against a stub engine, so no GPL engine binary is needed) and the desktop-proxy round
# trip (which writes machine-wide state, so the test that matters is that it is restored).
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

    qt_add_executable(test_systemproxy
        tests/test_systemproxy.cpp
        src/platform/SystemProxy.cpp
        src/platform/SystemProxy.h
    )
    target_link_libraries(test_systemproxy PRIVATE Qt6::Test)
    add_test(NAME systemproxy COMMAND test_systemproxy)
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
