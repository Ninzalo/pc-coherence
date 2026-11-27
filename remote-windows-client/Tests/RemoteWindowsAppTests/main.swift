import Foundation

// Client test runner (see Harness.swift for the rationale: no XCTest in a
// Command-Line-Tools-only environment). Exit code 0 = all suites pass.
// Unbuffered stdout: `swift run ... | tail` must not swallow per-case
// output if a case ever hangs (block-buffered pipes lose it on kill).
setvbuf(stdout, nil, _IONBF, 0)

let harness = Harness()

harness.runAll(L10nSuite.cases)
harness.runAll(PreferencesSuite.cases)
harness.runAll(ProtocolCodecSuite.cases)
harness.runAll(ProtocolVectorSuite.cases)
harness.runAll(FramingSuite.cases)
harness.runAll(SignalingClientSuite.cases)
harness.runAll(CatalogLaunchSuite.cases)
harness.runAll(MediaFramingSuite.cases)
harness.runAll(MediaKeysSuite.cases)
harness.runAll(MediaAeadSuite.cases)
harness.runAll(MediaHandshakeSuite.cases)
harness.runAll(MediaStreamSuite.cases)
harness.runAll(VideoConstructionSuite.cases)
harness.runAll(ShortcutAdaptationSuite.cases)

let total = harness.checks
print("")
print("TOTAL: \(total) checks across 14 suites, \(harness.failures) failure(s)")
exit(harness.failures == 0 ? 0 : 1)
