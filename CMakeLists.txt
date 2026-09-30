cmake_minimum_required(VERSION 3.16)
project(AgentAura VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 6.2 is the floor: Ubuntu 22.04 LTS ships Qt 6.2 and the release packages are built there.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

# Protocol and model code with no widgets: linked into the app and into the tests, which drive
# it against a local server instead of the real gateway.
add_library(agentaura_core STATIC
    src/models/Claw.h
    src/theme/AppColors.h
    src/services/SecretStore.h
    src/services/SecretStore.cpp
    src/services/WebSocketFrame.h
    src/services/WebSocketFrame.cpp
    src/services/WebSocketClient.h
    src/services/WebSocketClient.cpp
    src/services/ChatService.h
    src/services/ChatService.cpp
    src/services/ChatFormat.h
    src/services/ChatFormat.cpp
)
target_include_directories(agentaura_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agentaura_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
# One version, set here. The About/diagnostic output and the gateway handshake read it, so a
# release cannot report a stale number that was typed into the source by hand.
target_compile_definitions(agentaura_core PUBLIC AGENTAURA_VERSION="${PROJECT_VERSION}")

# ── Keyring ─────────────────────────────────────────────────────────────────────────
# The refresh token lives in the desktop keyring through libsecret. Without it the app still
# builds and works, keeping the token in its settings file as it did before.
option(AGENTAURA_USE_LIBSECRET "Store the session in the desktop keyring (libsecret)" ON)
if(AGENTAURA_USE_LIBSECRET)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
    endif()
    if(LIBSECRET_FOUND)
        target_link_libraries(agentaura_core PRIVATE PkgConfig::LIBSECRET)
        target_compile_definitions(agentaura_core PRIVATE AGENTAURA_HAVE_LIBSECRET)
        message(STATUS "Keyring: libsecret ${LIBSECRET_VERSION}")
    else()
        message(WARNING "libsecret-1 not found: the session will be stored in the settings file. "
                        "Install libsecret-1-dev, or pass -DAGENTAURA_USE_LIBSECRET=OFF to silence this.")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/models/AppUser.h
    src/models/ApiModels.h
    src/services/AgentLimit.h
    src/services/ApiClient.h
    src/services/ApiClient.cpp
    src/services/AuthState.h
    src/services/FirebaseAuth.h
    src/services/FirebaseAuth.cpp
    src/widgets/StatusBadge.h
    src/widgets/StatusBadge.cpp
    src/widgets/ClawCard.h
    src/widgets/ClawCard.cpp
    src/screens/ChatScreen.h
    src/screens/ChatScreen.cpp
    src/screens/ClawsScreen.h
    src/screens/ClawsScreen.cpp
    src/screens/DeployScreen.h
    src/screens/DeployScreen.cpp
    src/screens/LoginDialog.h
    src/screens/LoginDialog.cpp
    src/screens/AccountScreen.h
    src/screens/AccountScreen.cpp
    src/screens/ConfigScreen.h
    src/screens/ConfigScreen.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

qt_add_executable(AgentAura ${SOURCES})

# The window icon. On X11 the window manager reads it from the window; on Wayland the
# compositor matches the desktop file instead (see setDesktopFileName in main.cpp).
qt_add_resources(AgentAura "app_icon"
    PREFIX "/icons"
    BASE packaging/icons
    FILES packaging/icons/agentaura-256.png
)

target_link_libraries(AgentAura PRIVATE agentaura_core Qt6::Widgets Qt6::Network)
# ── Service endpoints ───────────────────────────────────────────────────────────────
# The defaults are AgentAura's own public service, the same values every official client
# ships with. A fork that runs its own backend overrides them at configure time:
#   cmake -B build -DAGENTAURA_API_BASE_URL=https://api.example.com \
#                  -DAGENTAURA_FIREBASE_API_KEY=<your Firebase Web API key>
set(AGENTAURA_API_BASE_URL "https://awsapi.allianceinterstellar.com"
    CACHE STRING "Base URL of the AgentAura (ClawHost) API")
# A Firebase Web API key identifies a Firebase project; it is public by design and is not a
# secret. What it can do is governed by Firebase Auth and security rules.
set(AGENTAURA_FIREBASE_API_KEY "AIzaSyBdKqX4ZPKnw1sM1c09_dGtiBJlFV13iSs"
    CACHE STRING "Firebase Web API key of the AgentAura project")
target_compile_definitions(AgentAura PRIVATE
    AGENTAURA_API_BASE_URL="${AGENTAURA_API_BASE_URL}"
    AGENTAURA_FIREBASE_API_KEY="${AGENTAURA_FIREBASE_API_KEY}")

set_target_properties(AgentAura PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ── Unit tests ──────────────────────────────────────────────────────────────────────
# Logic checked without a window or the real service: protocol code runs against a local
# WebSocket server started by the test itself. QtTest ships in qt6-base-dev, so nothing beyond
# the build dependencies is needed. Run with
#   ctest --test-dir build --output-on-failure
# They are not installed and do not end up in the packages.
option(AGENTAURA_BUILD_TESTS "Build the unit tests" ON)
if(AGENTAURA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(agent_limit_test tests/agent_limit_test.cpp)
    target_include_directories(agent_limit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(agent_limit_test PRIVATE Qt6::Core)
    add_test(NAME agent_limit COMMAND agent_limit_test)

    foreach(name tst_websocketframe tst_websocketclient tst_chatservice tst_chatformat tst_claw)
        add_executable(${name} tests/${name}.cpp tests/MiniWsServer.h)
        target_link_libraries(${name} PRIVATE agentaura_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
        # No display needed: these create no windows.
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

# ── Linux installation ──────────────────────────────────────────────────────────────
# The layout a desktop expects: the binary on PATH, a .desktop entry that names the
# icon, the icon in every hicolor size we have, and AppStream metadata for software
# centres. The release packages are assembled from exactly this install tree.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set_target_properties(AgentAura PROPERTIES OUTPUT_NAME agentaura)
    install(TARGETS AgentAura RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/io.allianceinterstellar.AgentAura.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/io.allianceinterstellar.AgentAura.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 32 64 128 256 512)
        install(FILES packaging/icons/hicolor/${size}x${size}/io.allianceinterstellar.AgentAura.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
endif()
