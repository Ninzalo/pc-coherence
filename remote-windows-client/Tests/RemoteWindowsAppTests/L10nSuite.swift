import Foundation
@testable import RemoteWindowsCore

enum L10nSuite {
    static let cases: [Harness.Case] = [
        .case("englishResourcesLoad") {
            harness.expect(!L10n.allKeys.isEmpty,
                           "Localizable.en.json must load from the app bundle")
        },
        .case("knownKeysResolveToEnglish") {
            harness.expectEqual(L10n.t("app.title"), "Remote Windows Client")
            harness.expectEqual(L10n.t("status.disconnected"), "Disconnected")
        },
        .case("unknownKeyFallsBackToKey") {
            harness.expectEqual(L10n.t("no.such.key"), "no.such.key")
        },
        .case("connectionStringsResolve") {
            harness.expectEqual(L10n.t("status.pairing"),
                                "Pairing — enter the 6-digit code shown on the Windows Host")
            harness.expectEqual(L10n.t("status.paired"), "Paired")
            harness.expectEqual(L10n.t("status.locked"), "Locked")
            harness.expectEqual(L10n.t("connect.button"), "Connect")
            harness.expectEqual(L10n.t("catalog.empty"), "No applications available yet")
        },
        .case("catalogStringsResolve") {
            harness.expectEqual(L10n.t("catalog.title"), "Applications")
            harness.expectEqual(L10n.t("catalog.search.placeholder"),
                                "Search applications")
            harness.expectEqual(L10n.t("catalog.launch"), "Launch")
            harness.expectEqual(L10n.t("catalog.launching"), "Launching…")
            harness.expectEqual(L10n.t("catalog.started"), "Started")
            harness.expectEqual(L10n.t("catalog.running"), "Running")
            harness.expectEqual(L10n.t("catalog.closed"), "Closed")
            harness.expectEqual(L10n.t("catalog.failed"), "Failed: %@")
            harness.expectEqual(L10n.t("catalog.unavailable"), "Unavailable")
            harness.expectEqual(L10n.t("catalog.column.name"), "Name")
            harness.expectEqual(L10n.t("catalog.column.status"), "Status")
            harness.expectEqual(L10n.t("catalog.launchBlocked"),
                                "Launch is available only while paired with the Windows Host")
        },
    ]
}
