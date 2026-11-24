import Foundation
@testable import RemoteWindowsCore

/// Minimal assertion harness for the Client test suites.
///
/// Why not XCTest: this development machine has the Swift Command Line
/// Tools only (no Xcode), and the XCTest framework ships with Xcode, not
/// the CLT. The suites therefore run as a plain executable:
/// `swift run RemoteWindowsAppTests` (exit code 0 = all pass). If Xcode
/// becomes available the same cases port 1:1 to XCTest (docs/STATUS.md).
final class Harness {
    private(set) var checks = 0
    private(set) var failures = 0
    private var current: String = "-"

    struct Case {
        let name: String
        let body: () throws -> Void
        init(_ name: String, _ body: @escaping () throws -> Void) {
            self.name = name
            self.body = body
        }
        static func `case`(_ name: String, _ body: @escaping () throws -> Void) -> Case {
            Case(name, body)
        }
    }

    func runAll(_ cases: [Case]) {
        for c in cases {
            current = c.name
            do {
                try c.body()
                print("PASS \(c.name)")
            } catch {
                failures += 1
                print("FAIL \(c.name): \(error)")
            }
        }
    }

    @discardableResult
    func expect(_ condition: @autoclosure () -> Bool,
                _ message: String = "",
                file: String = #file, line: Int = #line) -> Bool {
        checks += 1
        if condition() { return true }
        failures += 1
        print("  FAIL [\(current)] \(file):\(line) \(message.isEmpty ? "expectation failed" : message)")
        return false
    }

    func expectEqual<T: Equatable>(_ a: @autoclosure () -> T,
                                   _ b: @autoclosure () -> T,
                                   _ message: String = "",
                                   file: String = #file, line: Int = #line) {
        checks += 1
        let (x, y) = (a(), b())
        if x != y {
            failures += 1
            print("  FAIL [\(current)] \(file):\(line) \(message.isEmpty ? "" : message + " ")expected \(y), got \(x)")
        }
    }

    /// Expect `body` to throw a `CodecError` with exactly `code`.
    func expectCodecError(_ code: String,
                          _ body: () throws -> Envelope,
                          file: String = #file, line: Int = #line) {
        checks += 1
        do {
            _ = try body()
            failures += 1
            print("  FAIL [\(current)] \(file):\(line) expected CodecError(\(code)), none thrown")
        } catch let e as CodecError {
            if e.code != code {
                failures += 1
                print("  FAIL [\(current)] \(file):\(line) expected violation \(code), got \(e.code) (\(e.message))")
            }
        } catch {
            failures += 1
            print("  FAIL [\(current)] \(file):\(line) expected CodecError(\(code)), got \(error)")
        }
    }

    /// Fail the current case (like XCTSkip/XCTFail-then-abort).
    func fail(_ message: String, file: String = #file, line: Int = #line) throws -> Never {
        failures += 1
        print("  FAIL [\(current)] \(file):\(line) \(message)")
        throw HarnessAbort(message: message)
    }
}

/// Control-flow error that aborts a single case after a hard failure.
struct HarnessAbort: Error, CustomStringConvertible {
    let message: String
    var description: String { message }
}
