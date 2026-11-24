// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "RemoteWindows",
    defaultLocalization: "en",
    platforms: [
        .macOS(.v13),
    ],
    targets: [
        // Platform-independent Client services: protocol adapter, framing,
        // validation, FPS policy (later), preferences, localization. Shared
        // by the app executable and the test runner. Swift 5 language mode:
        // strict-concurrency adoption is a tracked follow-up (docs/STATUS.md).
        .target(
            name: "RemoteWindowsCore",
            path: "Sources/RemoteWindowsCore",
            resources: [
                .process("Resources"),
            ],
            swiftSettings: [
                .swiftLanguageMode(.v5),
            ],
        ),
        // The macOS Client executable (AppKit bootstrap + feature UI).
        .executableTarget(
            name: "RemoteWindowsApp",
            dependencies: ["RemoteWindowsCore"],
            path: "Sources/RemoteWindowsApp",
            swiftSettings: [
                .swiftLanguageMode(.v5),
            ],
        ),
        // Test runner: a plain executable. Why not `swift test`/XCTest: this
        // development machine has the Swift Command Line Tools only (no
        // Xcode), and the XCTest framework ships with Xcode, not the CLT.
        // The suites use a minimal assertion harness (Tests/.../Harness.swift);
        // run with `swift run RemoteWindowsAppTests` (exit 0 = all pass).
        .executableTarget(
            name: "RemoteWindowsAppTests",
            dependencies: ["RemoteWindowsCore"],
            path: "Tests/RemoteWindowsAppTests",
            swiftSettings: [
                .swiftLanguageMode(.v5),
            ],
        ),
    ],
)
