import AppKit
import RemoteWindowsCore

/// Owns the main application window. In the scaffold it shows the product
/// title, client version, configured Windows Host address, and the current
/// connection state. No feature controls are shown that do not perform a
/// real action (plan rule 6) — they are added with their owning feature
/// tasks (C-CONNECTION, C-CATALOG, ...).
final class AppController: NSObject, NSWindowDelegate {
    private var window: NSWindow?
    private let preferences: Preferences

    init(preferences: Preferences = .shared) {
        self.preferences = preferences
        super.init()
    }

    func showMainWindow() {
        let win = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 480, height: 220),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        win.title = L10n.t("app.title")
        win.isReleasedWhenClosed = false
        win.delegate = self

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false

        let title = makeLabel(L10n.t("app.title"), bold: true)
        let version = makeLabel(L10n.t("app.versionLabel") + " " + AppController.clientVersion)
        let host = makeLabel(
            L10n.t("pref.host_address_label") + ": "
            + preferences.hostAddress + ":" + String(preferences.hostPort)
        )
        let state = makeLabel(
            L10n.t("status.label") + ": " + L10n.t("status.disconnected")
        )
        stack.addArrangedSubview(title)
        stack.addArrangedSubview(version)
        stack.addArrangedSubview(host)
        stack.addArrangedSubview(state)

        let content = win.contentView!
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
        ])

        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
    }

    static let clientVersion = "0.1.0"

    private func makeLabel(_ text: String, bold: Bool = false) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        if bold {
            label.font = NSFont.boldSystemFont(ofSize: NSFont.systemFontSize + 2)
        }
        return label
    }
}
