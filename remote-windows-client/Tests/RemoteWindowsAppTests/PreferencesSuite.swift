import Foundation
@testable import RemoteWindowsCore

enum PreferencesSuite {
    private static func makePreferences() -> Preferences {
        let suite = "rwh-client-tests-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        return Preferences(defaults: defaults)
    }

    static let cases: [Harness.Case] = [
        .case("defaults") {
            let p = makePreferences()
            harness.expectEqual(p.hostAddress, "127.0.0.1")
            harness.expectEqual(p.hostPort, Preferences.defaultHostPort)
            harness.expectEqual(Preferences.defaultHostPort, 47987)
            harness.expectEqual(p.inactiveFps, 10, "FR-09: inactive_fps defaults to 10")
            harness.expect(p.pauseMinimizedStreams)
            harness.expect(p.adaptShortcutsMaster)
            harness.expectEqual(Preferences.settingsSchemaVersion, 1)
        },
        .case("inactiveFpsClampedToAllowedRange") {
            let p = makePreferences()
            p.inactiveFps = 0
            harness.expectEqual(p.inactiveFps, 1)
            p.inactiveFps = 240
            harness.expectEqual(p.inactiveFps, 60)
            p.inactiveFps = 30
            harness.expectEqual(p.inactiveFps, 30)
        },
        .case("persistenceAcrossInstances") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            let a = Preferences(defaults: defaults)
            a.inactiveFps = 15
            a.hostAddress = "192.168.100.139"
            a.adaptShortcutsMaster = false
            let b = Preferences(defaults: defaults)
            harness.expectEqual(b.inactiveFps, 15)
            harness.expectEqual(b.hostAddress, "192.168.100.139")
            harness.expect(!b.adaptShortcutsMaster)
            defaults.removePersistentDomain(forName: suite)
        },
    ]
}
