import AppKit
import RemoteWindowsCore

/// Owns the main application window and the connection/pairing/catalog UI
/// (task RW-C-001, extended by RW-C-002, role C-CATALOG). Every control
/// performs a real action (plan rule 6): Connect/Disconnect drive the
/// `SignalingClient`, the pairing-code field feeds the pairing handshake
/// against the Windows Host, the status/host labels mirror the live
/// session state, and the catalog section (search field, table, Launch
/// button) lists the host's real app catalog with per-app launch state.
/// No decorative controls.
final class AppController: NSObject, NSWindowDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
    private var window: NSWindow?
    private let preferences: Preferences
    private let client: SignalingClient
    private let model = CatalogLaunchModel()

    private var stateLabel: NSTextField!
    private var hostNameLabel: NSTextField!
    private var hostAddressField: NSTextField!
    private var pairingCodeField: NSTextField!
    private var connectButton: NSButton!
    private var disconnectButton: NSButton!
    private var submitCodeButton: NSButton!

    // Catalog section (RW-C-002): replaces the old count-only label.
    private var catalogSection: NSStackView!
    private var catalogHeaderLabel: NSTextField!
    private var catalogCountLabel: NSTextField!
    private var catalogStatusLabel: NSTextField!
    private var searchField: NSSearchField!
    private var catalogTable: NSTableView!
    private var catalogTableScroll: NSScrollView!
    private var catalogLaunchRow: NSStackView!
    private var launchButton: NSButton!

    private static let catalogNameColumnID = NSUserInterfaceItemIdentifier("catalog.name")
    private static let catalogStatusColumnID = NSUserInterfaceItemIdentifier("catalog.status")

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
        wiredClient.onCatalog = { [weak self] catalog in
            DispatchQueue.main.async {
                self?.model.applyCatalog(catalog)
                self?.refreshCatalogUI()
            }
        }
        wiredClient.onLaunchResult = { [weak self] result in
            DispatchQueue.main.async {
                self?.model.applyLaunchResult(result)
                self?.refreshCatalogUI()
            }
        }
        wiredClient.onAppStatus = { [weak self] status in
            DispatchQueue.main.async {
                self?.handleAppStatus(status)
            }
        }
    }

    deinit {
        client.disconnect()
    }

    func showMainWindow() {
        let win = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 560),
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
        hostNameLabel.textColor = .secondaryLabelColor

        let catalogSection = makeCatalogSection()

        [title, version, hostRow, codeRow, stateLabel, hostNameLabel, catalogSection]
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

    // MARK: Catalog section (RW-C-002)

    /// Search field + sorted table + Launch button. The section is
    /// hidden while not paired; an empty catalog keeps the honest empty
    /// message instead of a table (no fake rows).
    private func makeCatalogSection() -> NSStackView {
        catalogHeaderLabel = makeLabel(L10n.t("catalog.title"), bold: true)
        catalogCountLabel = makeLabel("")
        catalogCountLabel.textColor = .secondaryLabelColor
        let catalogHeaderRow = NSStackView(views: [catalogHeaderLabel, catalogCountLabel])
        catalogHeaderRow.orientation = .horizontal
        catalogHeaderRow.spacing = 8

        // Live search: filters by case-insensitive substring on
        // display_name as the user types.
        searchField = NSSearchField()
        searchField.translatesAutoresizingMaskIntoConstraints = false
        searchField.placeholderString = L10n.t("catalog.search.placeholder")
        searchField.delegate = self
        searchField.widthAnchor.constraint(equalToConstant: 520).isActive = true

        catalogTable = NSTableView()
        let nameColumn = NSTableColumn(identifier: Self.catalogNameColumnID)
        nameColumn.title = L10n.t("catalog.column.name")
        nameColumn.width = 320
        nameColumn.minWidth = 120
        let statusColumn = NSTableColumn(identifier: Self.catalogStatusColumnID)
        statusColumn.title = L10n.t("catalog.column.status")
        statusColumn.width = 180
        statusColumn.minWidth = 100
        catalogTable.addTableColumn(nameColumn)
        catalogTable.addTableColumn(statusColumn)
        catalogTable.usesAlternatingRowBackgroundColors = true
        catalogTable.allowsMultipleSelection = false
        catalogTable.dataSource = self
        catalogTable.delegate = self
        catalogTableScroll = NSScrollView()
        catalogTableScroll.documentView = catalogTable
        catalogTableScroll.hasVerticalScroller = true
        catalogTableScroll.autohidesScrollers = true
        catalogTableScroll.translatesAutoresizingMaskIntoConstraints = false
        catalogTableScroll.widthAnchor.constraint(equalToConstant: 520).isActive = true
        catalogTableScroll.heightAnchor.constraint(equalToConstant: 200).isActive = true

        launchButton = makeButton(L10n.t("catalog.launch"),
                                  action: #selector(launchAction))
        catalogStatusLabel = makeLabel("")
        catalogStatusLabel.textColor = .secondaryLabelColor
        catalogLaunchRow = NSStackView(views: [launchButton, catalogStatusLabel])
        catalogLaunchRow.orientation = .horizontal
        catalogLaunchRow.spacing = 8

        let section = NSStackView(views: [catalogHeaderRow, searchField,
                                          catalogTableScroll, catalogLaunchRow])
        section.orientation = .vertical
        section.alignment = .leading
        section.spacing = 8
        section.isHidden = true
        catalogSection = section
        return section
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

    /// Launch the selected catalog entry by its stable `app_id` only
    /// (FR-02: the client never sends paths or commands). One click, one
    /// `launch_app` — no retry, no auto-resend.
    @objc private func launchAction() {
        guard let appID = selectedAppID(),
              let entry = model.entry(for: appID), entry.available else {
            catalogStatusLabel.stringValue = L10n.t("catalog.launchBlocked")
            NSSound.beep()
            return
        }
        guard client.state == .paired else {
            catalogStatusLabel.stringValue = L10n.t("catalog.launchBlocked")
            NSSound.beep()
            return
        }
        // Surface the state problem instead of a silent no-op (rule 6).
        guard client.launchApp(appID: appID) else {
            catalogStatusLabel.stringValue = L10n.t("catalog.launchBlocked")
            NSSound.beep()
            return
        }
        model.beginLaunch(appID: appID)
        refreshCatalogUI()
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
        // No stale launch state after the socket dies (input-state
        // discipline analog).
        if state == .disconnected {
            model.reset()
            model.searchText = ""
            searchField.stringValue = ""
            catalogStatusLabel.stringValue = ""
        }
        stateLabel.stringValue = statusText(for: state)
        hostNameLabel.stringValue = client.hostName.map {
            L10n.t("host.name") + ": " + $0
        } ?? ""
        connectButton.isEnabled = state == .disconnected
        disconnectButton.isEnabled = state != .disconnected
        let codeEnabled = state == .pairing || state == .rejected || state == .locked
        pairingCodeField.isEnabled = codeEnabled
        submitCodeButton.isEnabled = codeEnabled
        hostAddressField.isEnabled = state == .disconnected
        refreshCatalogUI()
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

    private func refreshCatalogUI() {
        let paired = client.state == .paired
        let hasEntries = model.entries.count > 0
        let listVisible = paired && hasEntries
        catalogSection.isHidden = !paired
        searchField.isHidden = !listVisible
        catalogTableScroll.isHidden = !listVisible
        catalogLaunchRow.isHidden = !listVisible
        if paired {
            catalogCountLabel.stringValue = hasEntries
                ? String(model.entries.count) + " " + L10n.t("catalog.available")
                : L10n.t("catalog.empty")
        }
        refreshTableRows()
    }

    /// Rebuild the table and re-validate the selection after a wholesale
    /// catalog replacement (onCatalog may fire with a selection live):
    /// keep the selection if the app survives the refresh, else clear it.
    private func refreshTableRows() {
        let previousAppID = selectedAppID()
        catalogTable.reloadData()
        if let appID = previousAppID,
           let index = model.visibleEntries.firstIndex(where: { $0.appID == appID }) {
            catalogTable.selectRowIndexes(IndexSet(integer: index),
                                          byExtendingSelection: false)
        } else {
            catalogTable.deselectAll(nil)
        }
        refreshLaunchButton()
    }

    private func refreshLaunchButton() {
        let appID = selectedAppID()
        let available = appID.flatMap { model.entry(for: $0)?.available } ?? false
        launchButton.isEnabled = client.state == .paired && available
    }

    private func selectedAppID() -> String? {
        let row = catalogTable.selectedRow
        guard row >= 0, row < model.visibleEntries.count else { return nil }
        return model.visibleEntries[row].appID
    }

    private func handleAppStatus(_ status: AppStatus) {
        if status.state == .closed, let entry = model.entry(for: status.appID) {
            catalogStatusLabel.stringValue =
                entry.displayName + ": " + L10n.t("catalog.closed")
        }
        model.applyAppStatus(status)
        refreshCatalogUI()
    }

    // MARK: Table data source / delegate

    func numberOfRows(in tableView: NSTableView) -> Int {
        model.visibleEntries.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        guard let tableColumn,
              row >= 0, row < model.visibleEntries.count else { return nil }
        let entry = model.visibleEntries[row]
        let identifier = NSUserInterfaceItemIdentifier("catalog.cell")
        let label: NSTextField
        if let reused = tableView.makeView(withIdentifier: identifier, owner: self)
            as? NSTextField {
            label = reused
        } else {
            let fresh = NSTextField(labelWithString: "")
            fresh.identifier = identifier
            fresh.lineBreakMode = .byTruncatingTail
            label = fresh
        }
        if tableColumn.identifier == Self.catalogNameColumnID {
            label.stringValue = entry.displayName
            // Unavailable entries are dimmed (the host reports them as
            // unavailable; the Launch button stays disabled for them).
            label.textColor = entry.available ? .labelColor : .secondaryLabelColor
        } else {
            let (text, color) = statusDisplay(for: entry)
            label.stringValue = text
            label.textColor = color
        }
        return label
    }

    /// Per-row availability + launch state (FR-01/FR-02). The client
    /// shows what the host reports — `started` stays `started` until the
    /// host says more; nothing is invented.
    private func statusDisplay(for entry: AppEntry) -> (String, NSColor) {
        guard entry.available else {
            return (L10n.t("catalog.unavailable"), .secondaryLabelColor)
        }
        switch model.launchState(for: entry.appID) {
        case .idle:
            return ("", .labelColor)
        case .launching:
            return (L10n.t("catalog.launching"), .labelColor)
        case .started:
            return (L10n.t("catalog.started"), .labelColor)
        case .running:
            return (L10n.t("catalog.running"), .labelColor)
        case .failed(let message):
            return (String(format: L10n.t("catalog.failed"), message), .labelColor)
        }
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        refreshLaunchButton()
    }

    // MARK: Search field (live filtering)

    func controlTextDidChange(_ notification: Notification) {
        guard (notification.object as? NSTextField) === searchField else { return }
        model.searchText = searchField.stringValue
        refreshTableRows()
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
