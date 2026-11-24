import AppKit
import RemoteWindowsCore

/// Owns the main application window and the connection/pairing UI
/// (task RW-C-001, role C-CONNECTION). Every control performs a real
/// action (plan rule 6): Connect/Disconnect drive the
/// `SignalingClient`, the pairing-code field feeds the pairing handshake
/// against the Windows Host, and the status/host/catalog labels mirror
/// the live session state. No decorative controls.
final class AppController: NSObject, NSWindowDelegate {
    private var window: NSWindow?
    private let preferences: Preferences
    private let client: SignalingClient

    private var stateLabel: NSTextField!
    private var hostNameLabel: NSTextField!
    private var catalogLabel: NSTextField!
    private var hostAddressField: NSTextField!
    private var pairingCodeField: NSTextField!
    private var connectButton: NSButton!
    private var disconnectButton: NSButton!
    private var submitCodeButton: NSButton!

    static let clientVersion = "0.1.0"

    init(preferences: Preferences = .shared,
         client: SignalingClient? = nil) {
        self.preferences = preferences
        let wiredClient = client ?? SignalingClient(
            transport: NWSignalingTransport(),
            config: .init(appVersion: AppController.clientVersion))
        self.client = wiredClient
        super.init()
        // Transport callbacks may arrive off the main queue — hop.
        wiredClient.onStateChanged = { [weak self] _ in
            DispatchQueue.main.async { self?.refreshUI() }
        }
        wiredClient.onCatalog = { [weak self] _ in
            DispatchQueue.main.async { self?.refreshUI() }
        }
    }

    deinit {
        client.disconnect()
    }

    func showMainWindow() {
        let win = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 300),
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
        stack.spacing = 14
        stack.translatesAutoresizingMaskIntoConstraints = false

        let title = makeLabel(L10n.t("app.title"), bold: true)
        let version = makeLabel(
            L10n.t("app.versionLabel") + " " + AppController.clientVersion)

        // Host address + connect controls row.
        hostAddressField = NSTextField(
            string: "\(preferences.hostAddress):\(preferences.hostPort)")
        hostAddressField.translatesAutoresizingMaskIntoConstraints = false
        hostAddressField.placeholderString = L10n.t("host.placeholder")
        hostAddressField.widthAnchor.constraint(equalToConstant: 260).isActive = true
        connectButton = makeButton(L10n.t("connect.button"),
                                   action: #selector(connectAction))
        disconnectButton = makeButton(L10n.t("disconnect.button"),
                                      action: #selector(disconnectAction))
        let hostRow = NSStackView(views: [
            makeLabel(L10n.t("pref.host_address_label")),
            hostAddressField,
            connectButton,
            disconnectButton,
        ])
        hostRow.orientation = .horizontal
        hostRow.spacing = 8

        // Pairing code row.
        pairingCodeField = NSTextField(string: "")
        pairingCodeField.translatesAutoresizingMaskIntoConstraints = false
        pairingCodeField.placeholderString = L10n.t("pairingCode.placeholder")
        pairingCodeField.widthAnchor.constraint(equalToConstant: 120).isActive = true
        submitCodeButton = makeButton(L10n.t("pairingCode.submit"),
                                      action: #selector(submitCodeAction))
        let codeRow = NSStackView(views: [
            makeLabel(L10n.t("pairingCode.label")),
            pairingCodeField,
            submitCodeButton,
        ])
        codeRow.orientation = .horizontal
        codeRow.spacing = 8

        stateLabel = makeLabel("")
        hostNameLabel = makeLabel("")
        catalogLabel = makeLabel("")
        hostNameLabel.textColor = .secondaryLabelColor
        catalogLabel.textColor = .secondaryLabelColor

        [title, version, hostRow, codeRow, stateLabel, hostNameLabel, catalogLabel]
            .forEach { stack.addArrangedSubview($0) }

        let content = win.contentView!
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
        ])

        win.center()
        win.makeKeyAndOrderFront(nil)
        window = win
        refreshUI()
    }

    // MARK: Actions (each performs a real effect)

    @objc private func connectAction() {
        guard let (host, port) = Self.parseHostAddress(hostAddressField.stringValue) else {
            stateLabel.stringValue = L10n.t("status.invalidAddress")
            NSSound.beep()
            return
        }
        preferences.hostAddress = host
        preferences.hostPort = port
        client.connect(host: host, port: port)
    }

    @objc private func disconnectAction() {
        client.disconnect()
    }

    @objc private func submitCodeAction() {
        let code = pairingCodeField.stringValue
            .trimmingCharacters(in: .whitespaces)
        guard SignalingClient.isValidPairingCodeFormat(code) else {
            stateLabel.stringValue = L10n.t("pairingCode.invalid")
            NSSound.beep()
            return
        }
        pairingCodeField.stringValue = ""
        client.submitPairingCode(code)
    }

    /// Accepts `host` or `host:port` (port 1...65535; default 47987).
    static func parseHostAddress(_ raw: String) -> (host: String, port: Int)? {
        let trimmed = raw.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return nil }
        let parts = trimmed.split(separator: ":")
        switch parts.count {
        case 1:
            return (String(parts[0]), Preferences.defaultHostPort)
        case 2:
            guard let port = Int(parts[1]), (1...65535).contains(port) else { return nil }
            return (String(parts[0]), port)
        default:
            return nil
        }
    }

    // MARK: UI refresh

    private func refreshUI() {
        let state = client.state
        stateLabel.stringValue = statusText(for: state)
        hostNameLabel.stringValue = client.hostName.map {
            L10n.t("host.name") + ": " + $0
        } ?? ""
        catalogLabel.stringValue = catalogText()
        connectButton.isEnabled = state == .disconnected
        disconnectButton.isEnabled = state != .disconnected
        let codeEnabled = state == .pairing || state == .rejected || state == .locked
        pairingCodeField.isEnabled = codeEnabled
        submitCodeButton.isEnabled = codeEnabled
        hostAddressField.isEnabled = state == .disconnected
    }

    private func statusText(for state: SignalingState) -> String {
        switch state {
        case .disconnected:
            return L10n.t("status.label") + ": " + L10n.t("status.disconnected")
        case .connecting:
            return L10n.t("status.label") + ": " + L10n.t("status.connecting")
        case .pairing:
            return L10n.t("status.label") + ": " + L10n.t("status.pairing")
        case .paired:
            return L10n.t("status.label") + ": " + L10n.t("status.paired")
        case .rejected, .locked:
            let key = state == .rejected ? "status.rejected" : "status.locked"
            // Machine error codes / kinds stay out of resources; the host's
            // safe_message is user-facing and is shown verbatim.
            var detail = ""
            if let result = client.lastPairingResult {
                detail = result.safeMessage.isEmpty ? result.errorCode : result.safeMessage
            }
            return L10n.t("status.label") + ": " + L10n.t(key)
                + (detail.isEmpty ? "" : ": " + detail)
        }
    }

    private func catalogText() -> String {
        guard client.state == .paired, let catalog = client.catalog else { return "" }
        if catalog.entries.isEmpty {
            return L10n.t("catalog.empty")
        }
        return String(catalog.entries.count) + " " + L10n.t("catalog.available")
    }

    // MARK: Window delegate

    func windowWillClose(_ notification: Notification) {
        client.disconnect()
    }

    // MARK: View helpers

    private func makeLabel(_ text: String, bold: Bool = false) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        if bold {
            label.font = NSFont.boldSystemFont(ofSize: NSFont.systemFontSize + 2)
        }
        return label
    }

    private func makeButton(_ title: String, action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.translatesAutoresizingMaskIntoConstraints = false
        return button
    }
}
