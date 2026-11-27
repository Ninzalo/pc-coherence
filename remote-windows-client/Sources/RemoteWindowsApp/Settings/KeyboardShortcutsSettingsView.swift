import AppKit
import RemoteWindowsCore

/// **Keyboard Shortcuts** settings section (FR-08 8.2/8.3, plan §6.1).
///
/// A thin AppKit renderer of `KeyboardShortcutsSettingsModel` — every
/// behavior (toggles, CRUD, validation, persistence) lives in the
/// Foundation-only model and is unit-tested; this view only displays
/// state and forwards user actions. Rendering itself is NOT RUN
/// headless (CLT, no display) — see docs/STATUS.md.
///
/// Contents (all copy via L10n keys):
/// - master toggle `Adapt macOS shortcuts for Windows`;
/// - the plan-mandated interception note (macOS-reserved shortcuts may
///   stay local — FR-07 / plan rule 24: the Client never claims it can
///   intercept OS-consumed shortcuts);
/// - Built-in Mappings: one row per rule (localized name, `⌘X` source,
///   `Ctrl+X` target, enable toggle);
/// - Custom Mappings: ordered list (label, `⌘X → Ctrl+X`, enable
///   toggle) + Add / Edit / Delete / Move up / Move down; validation
///   messages from `ShortcutRuleValidator` are shown, never swallowed.
final class KeyboardShortcutsSettingsView: NSView,
    NSTableViewDataSource, NSTableViewDelegate {
    private let model: KeyboardShortcutsSettingsModel

    private var builtInTable: NSTableView!
    private var customTable: NSTableView!
    private var customEmptyLabel: NSTextField!
    private var addButton: NSButton!
    private var editButton: NSButton!
    private var deleteButton: NSButton!
    private var moveUpButton: NSButton!
    private var moveDownButton: NSButton!
    private var statusLabel: NSTextField!

    private static let biNameColumn = NSUserInterfaceItemIdentifier("shortcuts.bi.name")
    private static let biSourceColumn = NSUserInterfaceItemIdentifier("shortcuts.bi.source")
    private static let biTargetColumn = NSUserInterfaceItemIdentifier("shortcuts.bi.target")
    private static let cuLabelColumn = NSUserInterfaceItemIdentifier("shortcuts.cu.label")
    private static let cuChordColumn = NSUserInterfaceItemIdentifier("shortcuts.cu.chord")
    private static let cellID = NSUserInterfaceItemIdentifier("shortcuts.cell")
    private static let biToggleID = NSUserInterfaceItemIdentifier("shortcuts.bi.toggle")
    private static let cuToggleID = NSUserInterfaceItemIdentifier("shortcuts.cu.toggle")
    private static let masterID = NSUserInterfaceItemIdentifier("shortcuts.master")

    init(model: KeyboardShortcutsSettingsModel) {
        self.model = model
        super.init(frame: .zero)
        build()
        reload()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    // MARK: Layout

    private func build() {
        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.translatesAutoresizingMaskIntoConstraints = false

        let master = NSButton(checkboxWithTitle: L10n.t("shortcuts.master.toggle"),
                              target: self, action: #selector(masterToggled))
        master.identifier = Self.masterID
        master.state = model.masterEnabled ? .on : .off

        let note = makeLabel(L10n.t("shortcuts.interception.note"))
        note.textColor = .secondaryLabelColor
        note.maximumNumberOfLines = 0
        note.preferredMaxLayoutWidth = 520
        note.widthAnchor.constraint(equalToConstant: 520).isActive = true

        let biHeader = makeLabel(L10n.t("shortcuts.builtIns.title"), bold: true)
        builtInTable = makeTable([
            (Self.biNameColumn, L10n.t("shortcuts.column.label"), 160),
            (Self.biSourceColumn, L10n.t("shortcuts.column.source"), 120),
            (Self.biTargetColumn, L10n.t("shortcuts.column.target"), 160),
        ])
        let builtInScroll = makeTableScroll(builtInTable)

        let cuHeader = makeLabel(L10n.t("shortcuts.customs.title"), bold: true)
        let scopeNote = makeLabel(L10n.t("shortcuts.customs.scopeNote"))
        scopeNote.textColor = .secondaryLabelColor
        scopeNote.maximumNumberOfLines = 0
        scopeNote.preferredMaxLayoutWidth = 520
        scopeNote.widthAnchor.constraint(equalToConstant: 520).isActive = true
        customEmptyLabel = makeLabel(L10n.t("shortcuts.customs.empty"))
        customEmptyLabel.textColor = .secondaryLabelColor
        customTable = makeTable([
            (Self.cuLabelColumn, L10n.t("shortcuts.column.label"), 160),
            (Self.cuChordColumn,
             L10n.t("shortcuts.column.source") + " → " + L10n.t("shortcuts.column.target"),
             260),
        ])
        let customScroll = makeTableScroll(customTable)
        self.customScroll = customScroll

        addButton = makeButton(L10n.t("shortcuts.custom.add"), #selector(addAction))
        editButton = makeButton(L10n.t("shortcuts.custom.edit"), #selector(editAction))
        deleteButton = makeButton(L10n.t("shortcuts.custom.delete"), #selector(deleteAction))
        moveUpButton = makeButton(L10n.t("shortcuts.custom.moveUp"), #selector(moveUpAction))
        moveDownButton = makeButton(L10n.t("shortcuts.custom.moveDown"), #selector(moveDownAction))
        let buttonRow = NSStackView(views: [
            addButton, editButton, deleteButton, moveUpButton, moveDownButton,
        ])
        buttonRow.orientation = .horizontal
        buttonRow.spacing = 8
        statusLabel = makeLabel("")
        statusLabel.textColor = .secondaryLabelColor
        statusLabel.maximumNumberOfLines = 0
        statusLabel.preferredMaxLayoutWidth = 520
        statusLabel.widthAnchor.constraint(equalToConstant: 520).isActive = true

        [master, note, biHeader, builtInScroll, cuHeader, scopeNote,
         customEmptyLabel, customScroll, buttonRow, statusLabel]
            .forEach { stack.addArrangedSubview($0) }

        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 0),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 0),
        ])
    }

    private var customScroll: NSScrollView!

    private func makeTable(
        _ columns: [(NSUserInterfaceItemIdentifier, String, CGFloat)]
    ) -> NSTableView {
        let table = NSTableView()
        for (id, title, width) in columns {
            let column = NSTableColumn(identifier: id)
            column.title = title
            column.width = width
            column.minWidth = 60
            table.addTableColumn(column)
        }
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = false
        table.dataSource = self
        table.delegate = self
        return table
    }

    private func makeTableScroll(_ table: NSTableView) -> NSScrollView {
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.widthAnchor.constraint(equalToConstant: 520).isActive = true
        scroll.heightAnchor.constraint(equalToConstant: 180).isActive = true
        return scroll
    }

    // MARK: Reload

    func reload() {
        builtInTable.reloadData()
        customTable.reloadData()
        let hasCustom = !model.customRules.isEmpty
        customEmptyLabel.isHidden = hasCustom
        customScroll.isHidden = !hasCustom
        statusLabel.stringValue = model.storeLoadWarning ?? ""
        refreshButtons()
    }

    private func refreshButtons() {
        let hasSelection = selectedCustomID() != nil
        editButton.isEnabled = hasSelection
        deleteButton.isEnabled = hasSelection
        moveUpButton.isEnabled = hasSelection
        moveDownButton.isEnabled = hasSelection
    }

    private func selectedCustomID() -> String? {
        let row = customTable.selectedRow
        guard row >= 0, row < model.customRules.count else { return nil }
        return model.customRules[row].id
    }

    /// The row's enable toggle (created once per row cell).
    private func ensureToggle(in table: NSTableView, row: Int,
                              identifier: NSUserInterfaceItemIdentifier,
                              action: Selector) -> NSButton? {
        guard let cell = table.view(atColumn: 0, row: row, makeIfNecessary: true)
            as? NSTableCellView else { return nil }
        if let existing = cell.subviews.first { $0.identifier == identifier } as? NSButton {
            return existing
        }
        let toggle = NSButton(checkboxWithTitle: "", target: self, action: action)
        toggle.identifier = identifier
        cell.addSubview(toggle)
        NSLayoutConstraint.activate([
            toggle.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            toggle.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return toggle
    }

    // MARK: Table data source / delegate

    func numberOfRows(in tableView: NSTableView) -> Int {
        tableView === builtInTable ? model.builtInRows.count : model.customRules.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        guard let tableColumn else { return nil }
        let cell: NSTableCellView
        if let reused = tableView.makeView(withIdentifier: Self.cellID, owner: self)
            as? NSTableCellView {
            cell = reused
        } else {
            cell = NSTableCellView()
            cell.identifier = Self.cellID
            let label = NSTextField(labelWithString: "")
            label.translatesAutoresizingMaskIntoConstraints = false
            label.lineBreakMode = .byTruncatingTail
            cell.addSubview(label)
            cell.textField = label
            NSLayoutConstraint.activate([
                label.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 24),
                label.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
                label.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
        }

        if tableView === builtInTable {
            let rowModel = model.builtInRows[row]
            cell.textField?.stringValue =
                tableColumn.identifier == Self.biNameColumn
                ? rowModel.displayName
                : tableColumn.identifier == Self.biSourceColumn
                ? ChordDisplay.macSource(rowModel.rule.source)
                : ChordDisplay.winTarget(rowModel.rule.target)
            if let toggle = ensureToggle(in: builtInTable, row: row,
                                         identifier: Self.biToggleID,
                                         action: #selector(builtInToggled)) {
                toggle.state = rowModel.enabled ? .on : .off
            }
        } else {
            let rule = model.customRules[row]
            cell.textField?.stringValue =
                tableColumn.identifier == Self.cuLabelColumn
                ? (rule.label.isEmpty ? rule.id : rule.label)
                : ChordDisplay.macSource(rule.source) + " → "
                    + ChordDisplay.winTarget(rule.target)
            if let toggle = ensureToggle(in: customTable, row: row,
                                         identifier: Self.cuToggleID,
                                         action: #selector(customToggled)) {
                toggle.state = rule.enabled ? .on : .off
            }
        }
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        refreshButtons()
    }

    // MARK: Actions

    @objc private func masterToggled(_ sender: NSButton) {
        guard sender.identifier == Self.masterID else { return }
        model.masterEnabled = sender.state == .on
    }

    @objc private func builtInToggled(_ sender: NSButton) {
        guard sender.identifier == Self.biToggleID else { return }
        let row = builtInTable.row(for: sender)
        guard row >= 0, row < model.builtInRows.count else { return }
        let rowModel = model.builtInRows[row]
        model.setBuiltInEnabled(id: rowModel.rule.id, enabled: sender.state == .on)
        builtInTable.reloadData()
    }

    @objc private func customToggled(_ sender: NSButton) {
        guard sender.identifier == Self.cuToggleID else { return }
        let row = customTable.row(for: sender)
        guard row >= 0, row < model.customRules.count else { return }
        let id = model.customRules[row].id
        let enable = sender.state == .on
        switch model.setCustomEnabled(id: id, enabled: enable) {
        case .success:
            statusLabel.stringValue = ""
        case .failure(let error):
            // Surface the conflict; do NOT flip the state silently.
            statusLabel.stringValue = model.localizedMessage(for: error)
            sender.state = enable ? .off : .on
        }
        reload()
    }

    @objc private func addAction() {
        showEditor(existing: nil)
    }

    @objc private func editAction() {
        guard let id = selectedCustomID() else { return }
        showEditor(existing: model.customRules.first { $0.id == id })
    }

    @objc private func deleteAction() {
        guard let id = selectedCustomID() else { return }
        model.deleteCustom(id: id)
        statusLabel.stringValue = ""
        reload()
    }

    @objc private func moveUpAction() {
        guard let id = selectedCustomID() else { return }
        model.moveCustom(id: id, direction: -1)
        reload()
    }

    @objc private func moveDownAction() {
        guard let id = selectedCustomID() else { return }
        model.moveCustom(id: id, direction: 1)
        reload()
    }

    // MARK: Custom rule editor

    /// Add/edit panel (FR-08 8.3: label, captured source, target with
    /// modifiers, enabled state; source + target preview before saving;
    /// validation messages shown, never swallowed).
    private func showEditor(existing: ShortcutRule?) {
        let panel = ShortcutRuleEditorPanel(
            model: model, existing: existing) { [weak self] saved in
            // On failure the panel stays open and shows the message;
            // the section status line only updates on success.
            guard let self, saved else { return }
            self.statusLabel.stringValue = ""
            self.reload()
        }
        panel.center()
        panel.makeKeyAndOrderFront(nil)
    }

    private func makeButton(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.translatesAutoresizingMaskIntoConstraints = false
        return button
    }

    private func makeLabel(_ text: String, bold: Bool = false) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        if bold {
            label.font = NSFont.boldSystemFont(ofSize: NSFont.systemFontSize + 1)
        }
        return label
    }
}

// MARK: - Custom rule editor panel

/// Self-contained add/edit panel for one custom mapping. All rules
/// logic goes through `KeyboardShortcutsSettingsModel` (unit-tested);
/// the panel only captures input and displays results.
final class ShortcutRuleEditorPanel: NSPanel {
    private let model: KeyboardShortcutsSettingsModel
    private let existingID: String?
    private let onSaved: (Bool) -> Void

    private var labelField: NSTextField!
    private var captureField: ShortcutCaptureField!
    private var targetPopup: NSPopUpButton!
    private var shiftCheck: NSButton!
    private var ctrlCheck: NSButton!
    private var altCheck: NSButton!
    private var winCheck: NSButton!
    private var enabledCheck: NSButton!
    private var preview: NSTextField!
    private var validation: NSTextField!

    init(model: KeyboardShortcutsSettingsModel, existing: ShortcutRule?,
         onSaved: @escaping (Bool) -> Void) {
        self.model = model
        self.existingID = existing?.id
        self.onSaved = onSaved
        super.init(contentRect: NSRect(x: 0, y: 0, width: 440, height: 330),
                   styleMask: [.titled, .closable],
                   backing: .buffered, defer: false)
        title = existing == nil
            ? L10n.t("shortcuts.custom.title.add")
            : L10n.t("shortcuts.custom.title.edit")
        build(existing: existing)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private func build(existing: ShortcutRule?) {
        labelField = NSTextField(string: existing?.label ?? "")
        labelField.placeholderString = L10n.t("shortcuts.custom.label.placeholder")
        labelField.widthAnchor.constraint(equalToConstant: 380).isActive = true

        captureField = ShortcutCaptureField()
        captureField.widthAnchor.constraint(equalToConstant: 380).isActive = true
        if let existing { captureField.setChord(existing.source) }
        captureField.onChordCaptured = { [weak self] _ in self?.refreshPreview() }

        targetPopup = NSPopUpButton()
        targetPopup.widthAnchor.constraint(equalToConstant: 140).isActive = true
        for vk in WinVk.supportedTargetVk.sorted(by: { WinVk.name($0) < WinVk.name($1) }) {
            let item = NSMenuItem(title: WinVk.name(vk), action: nil, keyEquivalent: "")
            item.representedObject = vk
            targetPopup.menu?.addItem(item)
        }
        targetPopup.target = self
        targetPopup.action = #selector(controlChanged)
        if let existing,
           let index = (0..<targetPopup.numberOfItems).first(where: {
               (targetPopup.item(at: $0)?.representedObject as? UInt32)
                   == existing.target.windowsVk
           }) {
            targetPopup.selectItem(at: index)
        }

        shiftCheck = makeCheck("Shift", bits: existing?.target.winModifiers)
        ctrlCheck = makeCheck("Ctrl", bits: existing?.target.winModifiers)
        altCheck = makeCheck("Alt", bits: existing?.target.winModifiers)
        winCheck = makeCheck("Win", bits: existing?.target.winModifiers)

        preview = NSTextField(labelWithString: "")
        validation = NSTextField(labelWithString: "")
        validation.textColor = .secondaryLabelColor

        enabledCheck = NSButton(checkboxWithTitle: L10n.t("shortcuts.custom.enabled"),
                                target: nil, action: nil)
        enabledCheck.state = (existing?.enabled ?? true) ? .on : .off

        let saveButton = NSButton(title: L10n.t("shortcuts.custom.save"),
                                  target: self, action: #selector(save))
        saveButton.keyEquivalent = "\r"
        let cancelButton = NSButton(title: L10n.t("shortcuts.custom.cancel"),
                                    target: self, action: #selector(cancel))
        cancelButton.keyEquivalent = "\u{1b}"

        let form = NSStackView(views: [
            makeLabel(L10n.t("shortcuts.custom.label")),
            labelField,
            makeLabel(L10n.t("shortcuts.custom.source")),
            captureField,
            makeLabel(L10n.t("shortcuts.custom.target")),
            NSStackView(views: [targetPopup,
                                NSStackView(views: [shiftCheck, ctrlCheck,
                                                   altCheck, winCheck])]),
            NSStackView(views: [preview, validation]),
            enabledCheck,
            NSStackView(views: [saveButton, cancelButton]),
        ])
        form.orientation = .vertical
        form.alignment = .leading
        form.spacing = 6
        form.translatesAutoresizingMaskIntoConstraints = false

        let content = contentView!
        content.addSubview(form)
        NSLayoutConstraint.activate([
            form.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            form.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            form.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
        ])
        refreshPreview()
    }

    private func makeCheck(_ title: String, bits: UInt32?) -> NSButton {
        let check = NSButton(checkboxWithTitle: title, target: self,
                             action: #selector(controlChanged))
        switch (title, bits ?? 0) {
        case ("Shift", let b) where b & WinMod.shift != 0: check.state = .on
        case ("Ctrl", let b) where b & WinMod.control != 0: check.state = .on
        case ("Alt", let b) where b & WinMod.alt != 0: check.state = .on
        case ("Win", let b) where b & WinMod.win != 0: check.state = .on
        default: check.state = .off
        }
        return check
    }

    private func makeLabel(_ text: String) -> NSTextField {
        NSTextField(labelWithString: text)
    }

    private func currentTarget() -> TargetChord {
        var mods: UInt32 = 0
        if shiftCheck.state == .on { mods |= WinMod.shift }
        if ctrlCheck.state == .on { mods |= WinMod.control }
        if altCheck.state == .on { mods |= WinMod.alt }
        if winCheck.state == .on { mods |= WinMod.win }
        let vk = (targetPopup.selectedItem?.representedObject as? UInt32) ?? 0
        return TargetChord(windowsVk: vk, winModifiers: mods)
    }

    private func refreshPreview() {
        let source = captureField.chord
        let target = currentTarget()
        let src = (source.keyCode == nil && source.macModifiers == 0)
            ? "—" : ChordDisplay.macSource(source)
        preview.stringValue = src + " → "
            + (target.isEmpty ? "—" : ChordDisplay.winTarget(target))
    }

    @objc private func controlChanged() {
        refreshPreview()
    }

    @objc private func save() {
        let source = captureField.chord
        let target = currentTarget()
        let result: Result<ShortcutRule, ShortcutValidationError>
        if let id = existingID {
            if let existing = model.customRules.first(where: { $0.id == id }) {
                result = model.updateCustom(id: id,
                                            label: labelField.stringValue,
                                            source: source, target: target,
                                            enabled: enabledCheck.state == .on,
                                            scope: existing.scope)
            } else {
                result = .failure(.emptyTrigger)
            }
        } else {
            result = model.createCustom(label: labelField.stringValue,
                                        source: source, target: target,
                                        enabled: enabledCheck.state == .on)
        }
        switch result {
        case .success:
            onSaved(true)
            close()
        case .failure(let error):
            // Show the explicit validation message — never swallow it.
            validation.stringValue = model.localizedMessage(for: error)
            NSSound.beep()
        }
    }

    @objc private func cancel() {
        close()
    }
}

// MARK: - Source chord capture field

/// An NSTextField that captures the next key press as a source chord
/// (FR-08 8.3: "a source shortcut captured on macOS"). Physical key
/// code + device-independent modifier flags only — never characters.
final class ShortcutCaptureField: NSTextField {
    var chord: SourceChord = SourceChord(keyCode: nil, macModifiers: 0) {
        didSet {
            stringValue = (chord.keyCode == nil && chord.macModifiers == 0)
                ? L10n.t("shortcuts.custom.source.capture")
                : ChordDisplay.macSource(chord)
            onChordCaptured?(chord)
        }
    }
    var onChordCaptured: ((SourceChord) -> Void)?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        isEditable = false
        isSelectable = true
        placeholderString = L10n.t("shortcuts.custom.source.capture")
        stringValue = L10n.t("shortcuts.custom.source.capture")
        wantsLayer = true
        addGestureRecognizer(NSClickGestureRecognizer(
            target: self, action: #selector(armCapture)))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not supported")
    }

    private var armed = false

    @objc private func armCapture() {
        window?.makeFirstResponder(self)
        armed = true
    }

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        guard armed, event.type == .keyDown else {
            super.keyDown(with: event)
            return
        }
        let mods = MacModifier.normalize(UInt32(event.modifierFlags.rawValue))
        let code = UInt16(event.keyCode)
        // A bare modifier press alone is not a usable chord; wait for
        // the actual key (the validator rejects modifier-only chords).
        if !Self.isModifierKeyCode(code) {
            chord = SourceChord(keyCode: code, macModifiers: mods)
            armed = false
        }
    }

    private static func isModifierKeyCode(_ code: UInt16) -> Bool {
        [MacKey.command, MacKey.rightCommand, MacKey.shift, MacKey.rightShift,
         MacKey.option, MacKey.rightOption, MacKey.control, MacKey.rightControl,
         MacKey.function].contains(code)
    }

    func setChord(_ newChord: SourceChord) {
        chord = newChord
    }
}
