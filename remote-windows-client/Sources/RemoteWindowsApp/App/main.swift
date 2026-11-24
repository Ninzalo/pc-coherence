import AppKit

// Plain AppKit bootstrap: the MVP scaffold runs as a native AppKit
// application (SwiftPM executable). SwiftUI is hosted inside AppKit views
// once feature views land (plan §3.2). Packaging as a .app bundle is a
// follow-up (docs/STATUS.md).
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.activate(ignoringOtherApps: true)
app.run()
