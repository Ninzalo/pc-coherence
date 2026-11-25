import Foundation
import CoreMedia
import VideoToolbox

/// H.264 decode construction (RW-C-003 test 6 — CONSTRUCTION ONLY):
/// the same objects `VideoPipeline` (RemoteWindowsApp target) creates
/// per stream — a `CMVideoFormatDescription` from the in-band SPS/PPS
/// (AVCC, 4-byte NAL header) plus a `VTDecompressionSession` — must
/// build from a minimal valid fixture.
///
/// Decoded PIXEL OUTPUT is NOT RUN in this headless Command-Line-Tools
/// environment (no display verification possible); construction is the
/// verifiable subset here (see the task report).
enum VideoConstructionSuite {
    // Minimal baseline-profile H.264 SPS/PPS fixture (classic test
    // vector). The fixture's resolution is not part of the protocol
    // contract — construction success is what is asserted.
    static let sps: [UInt8] = [
        0x67, 0x42, 0xC0, 0x1E, 0xD8, 0x84, 0x25, 0x48, 0xE9, 0x80,
        0xA0, 0x00, 0x11, 0x10, 0x00, 0x00, 0x03, 0x00, 0x80, 0x00,
        0x00, 0x19, 0xC8, 0x02, 0x00,
    ]
    static let pps: [UInt8] = [
        0x68, 0xCB, 0x88, 0x21, 0x9C, 0x02, 0x80, 0x00, 0x00, 0x16,
        0x40, 0x1C, 0x82, 0x01, 0x40, 0x2A,
    ]

    static let cases: [Harness.Case] = [
        .case("h264FormatDescriptionAndSessionConstruct") {
            do {
                // The same call VideoPipeline.makeFormatDescription
                // makes for the first keyframe's in-band SPS/PPS.
                let formatDescription = try CMVideoFormatDescription(
                    h264ParameterSets: [Data(sps), Data(pps)],
                    nalUnitHeaderLength: 4)
                let dims = CMVideoFormatDescriptionGetDimensions(formatDescription)
                harness.expect(dims.width > 0 && dims.height > 0,
                               "format description must report a size " +
                               "(got \(dims.width)x\(dims.height))")

                // The classic-C session create VideoPipeline uses
                // (null output-callback record; frames arrive via the
                // per-decode outputHandler).
                var session: VTDecompressionSession?
                let status = VTDecompressionSessionCreate(
                    allocator: nil, formatDescription: formatDescription,
                    decoderSpecification: nil, imageBufferAttributes: nil,
                    outputCallback: nil, decompressionSessionOut: &session)
                harness.expectEqual(status, noErr, "VTDecompressionSessionCreate")
                harness.expect(session != nil,
                               "decoder session must be constructed")
                // The CF session is released when `session` goes out of
                // scope (ARC); the SDK no longer exposes a Dispose.
            } catch {
                try harness.fail("H.264 construction failed: \(error)")
            }
        },
    ]
}
