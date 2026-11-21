# Test tooling only, never part of the app bundle.

# A software display for hot-plug tests without monitors (scripts/hotplug-test.sh).
# Uses CGVirtualDisplay, an undocumented macOS API.
enable_language(OBJC)
add_executable(virtual-display virtual-display.m)
target_compile_options(virtual-display PRIVATE -fobjc-arc)
target_link_libraries(virtual-display PRIVATE "-framework Foundation" "-framework CoreGraphics")
