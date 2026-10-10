cmake_minimum_required(VERSION 3.24)
project(TypeTide-Win VERSION 0.2.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TypeTide-Win requires Windows and the Windows SDK")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# RC dependency discovery can miss icon changes; always rebuild the embedded icon.
set_source_files_properties(src/TypeTide-Win.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico")

add_executable(TypeTide-Win WIN32
    src/main.cpp
    src/App.cpp
    src/Log.cpp
    src/LocalDiagnostics.cpp
    src/Json.cpp
    src/Language.cpp
    src/Settings.cpp
    src/Credentials.cpp
    src/Clipboard.cpp
    src/Keyboard.cpp
    src/Http.cpp
    src/Translator.cpp
    src/ProviderProtocol.cpp
    src/OllamaModels.cpp
    src/CrashDump.cpp
    src/SelectionCapture.cpp
    src/TextReplacer.cpp
    src/Hooks.cpp
    src/PopupWindow.cpp
    src/SelectionIcon.cpp
    src/SelectionMonitor.cpp
    src/TrayIcon.cpp
    src/SettingsWindow.cpp
    src/LaunchAtLogin.cpp
    src/UpdateChecker.cpp
    src/SelfTest.cpp
    src/TypeTide-Win.rc
    src/TypeTide-Win.manifest
)

target_compile_definitions(TypeTide-Win PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    TYPETIDE_VERSION_STRING="${PROJECT_VERSION}"
    TYPETIDE_CONTRACTS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../contracts/translation-contracts.json"
)

if(MSVC)
    target_compile_options(TypeTide-Win PRIVATE /W4 /utf-8 /EHsc)
endif()

target_link_libraries(TypeTide-Win PRIVATE
    user32 gdi32 shell32 ole32 oleaut32 uuid comctl32
    winhttp uiautomationcore dwmapi advapi32 shlwapi uxtheme dbghelp
    comdlg32 gdiplus
)

include(CTest)
if(BUILD_TESTING)
    add_executable(TypeTide-UiTests
        tests/PopupWindowTests.cpp src/PopupWindow.cpp src/Language.cpp
        src/Clipboard.cpp src/Hooks.cpp src/Log.cpp src/Settings.cpp src/Json.cpp src/Credentials.cpp)
    target_include_directories(TypeTide-UiTests PRIVATE src)
    target_compile_definitions(TypeTide-UiTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(TypeTide-UiTests PRIVATE /W4 /utf-8 /EHsc)
    endif()
    target_link_libraries(TypeTide-UiTests PRIVATE user32 gdi32 gdiplus dwmapi advapi32 ole32)
    add_test(NAME popup-presentation COMMAND TypeTide-UiTests)
    set_tests_properties(popup-presentation PROPERTIES
        ENVIRONMENT "TYPETIDE_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-test-data")
    add_executable(TypeTide-ProviderTests tests/ProviderTests.cpp src/ProviderProtocol.cpp
        src/Translator.cpp src/Http.cpp src/Settings.cpp src/Credentials.cpp src/Json.cpp
        src/Language.cpp src/Log.cpp src/LocalDiagnostics.cpp)
    target_include_directories(TypeTide-ProviderTests PRIVATE src)
    target_compile_definitions(TypeTide-ProviderTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(TypeTide-ProviderTests PRIVATE /W4 /utf-8 /EHsc)
    endif()
    target_link_libraries(TypeTide-ProviderTests PRIVATE user32 winhttp advapi32 ole32 ws2_32)
    add_test(NAME provider-routing COMMAND TypeTide-ProviderTests)
    set_tests_properties(provider-routing PROPERTIES TIMEOUT 30
        ENVIRONMENT "TYPETIDE_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/provider-test-data")
endif()
