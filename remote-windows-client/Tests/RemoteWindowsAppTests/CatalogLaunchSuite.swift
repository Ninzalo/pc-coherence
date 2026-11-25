import Foundation
@testable import RemoteWindowsCore

/// RW-C-002 (C-CATALOG): the launch lifecycle over the wire —
/// `launch_app` (paired-only, fresh request_id, no retry), `launch_result`
/// and `app_status` callbacks, and `app_catalog_changed` re-request
/// handling — driven through the same fake-transport pattern as
/// SignalingClientSuite. The AppKit catalog UI reads these decoded
/// payloads via `onLaunchResult`/`onAppStatus` on the main queue; the
/// UI itself is smoke-tested (tools/launch_smoke_test.sh) and
/// code-review-verified (no display in this environment).
enum CatalogLaunchSuite {
    static let testAppID = "notepad"
    /// 36-char host application_session_id shape (RW-H-008).
    static let sessionID = "a1b2c3d4e5f64a7b8c9d0e1f2a3b4c5d"

    static func launchResult(_ state: RWLaunchState,
                             appID: String = testAppID,
                             errorCode: String = "",
                             safeMessage: String = "",
                             sessionID: String? = nil) -> Envelope {
        Envelope(kind: .launchResult, requestID: 1,
                 payload: .launchResult(LaunchResult(
                     appID: appID, state: state, errorCode: errorCode,
                     safeMessage: safeMessage,
                     applicationSessionID: sessionID)))
    }

    static func appStatus(_ state: RWAppRunState,
                          appID: String = testAppID,
                          sessionID: String = "") -> Envelope {
        Envelope(kind: .appStatus,
                 payload: .appStatus(AppStatus(
                     appID: appID, state: state,
                     applicationSessionID: sessionID)))
    }

    /// Paired session with a two-entry catalog (one available, one not) —
    /// the real-catalog shape the host serves since RW-H-008.
    static func pairedWithCatalog() throws -> FakeSession {
        let s = FakeSession.make()
        try s.driveToPairing()
        harness.expect(s.client.submitPairingCode(FakeSession.testCode))
        try s.transport.deliver(FakeSession.pairingResult(accepted: true))
        harness.expectEqual(s.client.state, SignalingState.paired)
        try s.transport.deliver(Envelope(
            kind: .appCatalog, requestID: 1,
            payload: .appCatalog(AppCatalog(catalogRevision: 1, entries: [
                AppEntry(appID: "notepad", displayName: "Notepad",
                         type: .win32Shortcut),
                AppEntry(appID: "paint", displayName: "Paint",
                         type: .packaged, available: false),
            ]))))
        harness.expectEqual(s.client.catalog?.entries.count, 2)
        return s
    }

    static let cases: [Harness.Case] = [
        .case("launchAppWhilePairedSendsOneLaunchAppEnvelope") {
            let s = try Self.pairedWithCatalog()
            let before = s.transport.sendCount
            harness.expect(s.client.launchApp(appID: Self.testAppID),
                           "launchApp must succeed while paired")
            harness.expectEqual(s.transport.sendCount, before + 1,
                                "exactly one launch_app per click (no retry)")
            let envelope = s.transport.sentEnvelopes.last
            harness.expectEqual(envelope?.kind, RWKind.launchApp)
            harness.expect(envelope?.requestID != nil,
                           "launch_app carries a fresh request_id")
            if case .launchApp(let m)? = envelope?.payload {
                harness.expectEqual(m.appID, Self.testAppID,
                                    "launch_app carries the app_id only — never paths")
            } else {
                try harness.fail("expected the launch_app payload")
            }
            // A second click is a second, distinct request (fresh id each).
            let firstID = envelope?.requestID
            harness.expect(s.client.launchApp(appID: Self.testAppID))
            let second = s.transport.sentEnvelopes.last
            harness.expectEqual(second?.kind, RWKind.launchApp)
            harness.expect(second?.requestID != firstID,
                           "each launch_app gets a fresh request_id")
        },
        .case("launchAppOutsidePairedSendsNothing") {
            let s = FakeSession.make()
            // disconnected
            harness.expect(!s.client.launchApp(appID: Self.testAppID))
            harness.expectEqual(s.transport.sendCount, 0)
            // connecting (transport ready, HELLO sent)
            s.client.connect(host: "10.0.0.7", port: 47987)
            s.transport.fireReady()
            harness.expect(!s.client.launchApp(appID: Self.testAppID))
            harness.expectEqual(s.transport.sendCount, 1, "only the HELLO")
            // pairing
            try s.transport.deliver(FakeSession.hostHello())
            try s.transport.deliver(FakeSession.challenge())
            harness.expectEqual(s.client.state, SignalingState.pairing)
            harness.expect(!s.client.launchApp(appID: Self.testAppID))
            harness.expectEqual(s.transport.sendCount, 1,
                                "nothing sent while pairing")
        },
        .case("launchResultStartedFiresCallbackWithDecodedPayload") {
            let s = try Self.pairedWithCatalog()
            var results: [LaunchResult] = []
            s.client.onLaunchResult = { results.append($0) }
            try s.transport.deliver(Self.launchResult(
                .started, sessionID: Self.sessionID))
            harness.expectEqual(results.count, 1, "onLaunchResult fired once")
            harness.expectEqual(results.first?.appID, Self.testAppID)
            harness.expectEqual(results.first?.state, RWLaunchState.started)
            harness.expectEqual(results.first?.applicationSessionID, Self.sessionID)
            harness.expectEqual(s.client.droppedMessageCount, 0)
        },
        .case("launchResultFailedUnknownAppIdSurfacesErrorCode") {
            let s = try Self.pairedWithCatalog()
            var results: [LaunchResult] = []
            s.client.onLaunchResult = { results.append($0) }
            try s.transport.deliver(Self.launchResult(
                .failed, errorCode: "unknown_app_id",
                safeMessage: "Unknown application"))
            harness.expectEqual(results.count, 1)
            harness.expectEqual(results.first?.state, RWLaunchState.failed)
            harness.expectEqual(results.first?.errorCode, "unknown_app_id")
            harness.expectEqual(results.first?.safeMessage, "Unknown application")
        },
        .case("launchResultWhileUnpairedIsNotSurfaced") {
            let s = FakeSession.make()
            var results: [LaunchResult] = []
            s.client.onLaunchResult = { results.append($0) }
            s.transport.deliverRaw(try Framing.encode(
                try ProtocolCodec.encode(Self.launchResult(
                    .started, sessionID: Self.sessionID))))
            harness.expectEqual(results.count, 0,
                                "no launch events while unpaired")
            harness.expectEqual(s.client.droppedMessageCount, 0,
                                "a well-formed envelope is not a decode drop")
        },
        .case("appStatusRunningAndClosedFireCallback") {
            let s = try Self.pairedWithCatalog()
            var statuses: [AppStatus] = []
            s.client.onAppStatus = { statuses.append($0) }
            try s.transport.deliver(Self.appStatus(
                .running, sessionID: Self.sessionID))
            try s.transport.deliver(Self.appStatus(.closed))
            harness.expectEqual(statuses.count, 2, "both statuses surfaced")
            harness.expectEqual(statuses.first?.state, RWAppRunState.running)
            harness.expectEqual(statuses.first?.applicationSessionID, Self.sessionID)
            harness.expectEqual(statuses.last?.state, RWAppRunState.closed)
        },
        .case("appCatalogChangedWhilePairedReRequestsCatalogNonForced") {
            let s = try Self.pairedWithCatalog()
            let before = s.transport.sendCount
            try s.transport.deliver(Envelope(
                kind: .appCatalogChanged,
                payload: .appCatalogChanged(AppCatalogChanged(catalogRevision: 2))))
            harness.expectEqual(s.transport.sendCount, before + 1,
                                "one app_catalog_request per change notice")
            let envelope = s.transport.sentEnvelopes.last
            harness.expectEqual(envelope?.kind, RWKind.appCatalogRequest)
            harness.expect(envelope?.requestID != nil,
                           "the re-request carries a fresh request_id")
            if case .appCatalogRequest(let m)? = envelope?.payload {
                harness.expect(!m.forceRescan,
                               "re-request is non-forced: the host already knows")
            } else {
                try harness.fail("expected the app_catalog_request payload")
            }
            // The fresh snapshot then refills the client catalog.
            try s.transport.deliver(Envelope(
                kind: .appCatalog, requestID: envelope?.requestID,
                payload: .appCatalog(AppCatalog(catalogRevision: 2, entries: [
                    AppEntry(appID: "notepad", displayName: "Notepad",
                             type: .win32Shortcut),
                    AppEntry(appID: "calc", displayName: "Calculator",
                             type: .packaged),
                ]))))
            harness.expectEqual(s.client.catalog?.catalogRevision, UInt32(2))
            harness.expectEqual(s.client.catalog?.entries.count, 2)
        },
        .case("appCatalogChangedWhileUnpairedSendsNothing") {
            let s = FakeSession.make()
            s.transport.deliverRaw(try Framing.encode(
                try ProtocolCodec.encode(Envelope(
                    kind: .appCatalogChanged,
                    payload: .appCatalogChanged(AppCatalogChanged(catalogRevision: 1))))))
            harness.expectEqual(s.transport.sendCount, 0,
                                "no catalog re-request while unpaired")
            harness.expectEqual(s.client.droppedMessageCount, 0)
        },
    ]
}
