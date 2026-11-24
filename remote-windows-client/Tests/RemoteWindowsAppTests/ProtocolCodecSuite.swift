import Foundation
@testable import RemoteWindowsCore

enum ProtocolCodecSuite {
    private static let uuid = "7c9e6679-7425-40de-944b-e07fc1f90ae7"

    static let cases: [Harness.Case] = [
        .case("roundTripHello") {
            let e = Envelope(kind: .hello, payload: .hello(Hello(
                productName: "macos-client", appVersion: "0.1.0",
                capabilities: [.appCatalog, .input, .resize])))
            let back = try ProtocolCodec.decodeEnvelope(try ProtocolCodec.encode(e))
            harness.expectEqual(back, e)
        },
        .case("roundTripWindowAnnounceAuxiliary") {
            let parent = "11111111-2222-4333-8444-555555555555"
            let e = Envelope(kind: .windowAnnounce, payload: .windowAnnounce(WindowAnnounce(
                windowID: uuid, applicationSessionID: "sess-0001",
                parentWindowID: parent, role: .filePicker, modal: true,
                visible: true, zOrder: 5, title: "Open", x: 100, y: 200,
                widthPx: 800, heightPx: 600, dpiScalePercent: 150,
                state: .normal)))
            let back = try ProtocolCodec.decodeEnvelope(try ProtocolCodec.encode(e))
            harness.expectEqual(back, e)
        },
        .case("roundTripInputKeyEventWithSequence") {
            let e = Envelope(kind: .inputEvent, sequence: 42,
                             payload: .inputEvent(InputEvent(windowID: uuid,
                                                              input: .key(KeyInput(
                                                                  windowsVk: 0x0C, // C
                                                                  modifierBits: 0x2, // control
                                                                  keyDown: true)))))
            let back = try ProtocolCodec.decodeEnvelope(try ProtocolCodec.encode(e))
            harness.expectEqual(back, e)
            harness.expectEqual(back.sequence, 42)
        },
        .case("roundTripPowerRequest") {
            let e = Envelope(kind: .systemPowerRequest, requestID: 7, sequence: 1,
                             payload: .systemPowerRequest(SystemPowerRequest(action: .sleep)))
            let back = try ProtocolCodec.decodeEnvelope(try ProtocolCodec.encode(e))
            harness.expectEqual(back, e)
        },
        .case("roundTripAppCatalog") {
            let e = Envelope(kind: .appCatalog, requestID: 3, payload: .appCatalog(AppCatalog(
                catalogRevision: 2,
                entries: [
                    AppEntry(appID: "explorer", displayName: "File Explorer",
                             type: .win32Shortcut, available: true),
                    AppEntry(appID: "user-added_1", displayName: "My Tool",
                             type: .userAdded, iconRef: "icon:1", available: false),
                ])))
            let back = try ProtocolCodec.decodeEnvelope(try ProtocolCodec.encode(e))
            harness.expectEqual(back, e)
        },
        .case("unknownFieldsIgnored") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"heartbeat","unknown_field":123,
             "payload":{"sender_time_ms":5,"also_unknown":true}}
            """.data(using: .utf8)!
            let e = try ProtocolCodec.decodeEnvelope(json)
            harness.expectEqual(e.payload, .heartbeat(Heartbeat(senderTimeMs: 5)))
            harness.expect(ProtocolValidation.validate(e).ok)
        },
        .case("absentOptionalsDecodeAsNil") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"launch_result","request_id":9,
             "payload":{"app_id":"a0123456789abcdef","state":"accepted"}}
            """.data(using: .utf8)!
            let e = try ProtocolCodec.decodeEnvelope(json)
            guard case .launchResult(let m) = e.payload else {
                try harness.fail("wrong payload type")
            }
            harness.expect(m.applicationSessionID == nil)
            harness.expectEqual(m.errorCode, "")
        },
        .case("malformedJsonRejected") {
            harness.expectCodecError("malformed_json") {
                try ProtocolCodec.decodeEnvelope(Data("not json".utf8))
            }
            harness.expectCodecError("malformed_json") {
                try ProtocolCodec.decodeEnvelope(Data("[1,2]".utf8))
            }
        },
        .case("unknownKindRejected") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"bogus","payload":{}}
            """.data(using: .utf8)!
            harness.expectCodecError("unknown_kind") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("badUuidRejectedAtDecode") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"window_remove",
             "payload":{"window_id":"nope","reason":"closed"}}
            """.data(using: .utf8)!
            harness.expectCodecError("bad_uuid") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("titleTooLongRejectedAtDecode") {
            let longTitle = String(repeating: "x", count: 513)
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"window_update",
             "payload":{"window_id":"\(uuid)","title":"\(longTitle)"}}
            """.data(using: .utf8)!
            harness.expectCodecError("title_too_long") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("badOneofRejected") {
            let both = """
            {"schema_major":1,"schema_minor":0,"kind":"input_event","sequence":1,
             "payload":{"window_id":"\(uuid)",
                        "mouse":{"kind":"move","x":1,"y":2},
                        "key":{"windows_vk":1,"key_down":true}}}
            """.data(using: .utf8)!
            harness.expectCodecError("bad_oneof") {
                try ProtocolCodec.decodeEnvelope(both)
            }
            let neither = """
            {"schema_major":1,"schema_minor":0,"kind":"input_event","sequence":1,
             "payload":{"window_id":"\(uuid)"}}
            """.data(using: .utf8)!
            harness.expectCodecError("bad_oneof") {
                try ProtocolCodec.decodeEnvelope(neither)
            }
        },
        .case("fieldTypeRejected") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"heartbeat","payload":{"sender_time_ms":"5"}}
            """.data(using: .utf8)!
            harness.expectCodecError("field_type") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("intOverflowRejected") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"resize_request","request_id":1,"sequence":1,
             "payload":{"window_id":"\(uuid)","client_width_px":4294967296,"client_height_px":10}}
            """.data(using: .utf8)!
            harness.expectCodecError("int_overflow") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("unknownEnumRejected") {
            let json = """
            {"schema_major":1,"schema_minor":0,"kind":"system_power_request","request_id":1,"sequence":1,
             "payload":{"action":"reboot"}}
            """.data(using: .utf8)!
            harness.expectCodecError("invalid_enum") {
                try ProtocolCodec.decodeEnvelope(json)
            }
        },
        .case("validationRejectsSchemaMajorMismatch") {
            let e = Envelope(schemaMajor: 2, kind: .heartbeat,
                             payload: .heartbeat(Heartbeat(senderTimeMs: 1)))
            let r = ProtocolValidation.validate(e)
            harness.expect(!r.ok)
            harness.expectEqual(r.errorCode, "schema_major_must_be_1")
        },
        .case("validationRequiresRequestAndSequence") {
            let noReq = Envelope(kind: .launchApp,
                                 payload: .launchApp(LaunchApp(appID: "explorer")))
            harness.expectEqual(ProtocolValidation.validate(noReq).errorCode,
                                "missing_request_id")
            let noSeq = Envelope(kind: .inputEvent, sequence: nil,
                                 payload: .inputEvent(InputEvent(windowID: uuid,
                                                                 input: .key(KeyInput(
                                                                     windowsVk: 1, keyDown: true)))))
            harness.expectEqual(ProtocolValidation.validate(noSeq).errorCode,
                                "missing_sequence")
        },
        .case("validationRejectsUnspecifiedEnums") {
            let e = Envelope(kind: .windowRemove,
                             payload: .windowRemove(WindowRemove(windowID: uuid,
                                                                 reason: .unspecified)))
            harness.expectEqual(ProtocolValidation.validate(e).errorCode, "invalid_enum")
        },
        .case("validationRejectsWindowUpdateWithoutChange") {
            let e = Envelope(kind: .windowUpdate,
                             payload: .windowUpdate(WindowUpdate(windowID: uuid)))
            harness.expectEqual(ProtocolValidation.validate(e).errorCode, "empty_payload")
        },
        .case("validationRejectsFpsOutOfRange") {
            let e = Envelope(kind: .frameRateRequest, requestID: 1,
                             payload: .frameRateRequest(FrameRateRequest(
                                 windowID: uuid, targetFps: 241, reason: .focusChanged)))
            harness.expectEqual(ProtocolValidation.validate(e).errorCode, "fps_out_of_range")
        },
    ]
}
