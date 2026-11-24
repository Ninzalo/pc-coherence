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
    ]
}
