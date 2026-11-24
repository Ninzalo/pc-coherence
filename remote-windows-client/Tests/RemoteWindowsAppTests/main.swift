import Foundation

// Client test runner (see Harness.swift for the rationale: no XCTest in a
// Command-Line-Tools-only environment). Exit code 0 = all suites pass.

let harness = Harness()

harness.runAll(L10nSuite.cases)
harness.runAll(PreferencesSuite.cases)
harness.runAll(ProtocolCodecSuite.cases)
harness.runAll(ProtocolVectorSuite.cases)
harness.runAll(FramingSuite.cases)

let total = harness.checks
print("")
print("TOTAL: \(total) checks across 5 suites, \(harness.failures) failure(s)")
exit(harness.failures == 0 ? 0 : 1)
