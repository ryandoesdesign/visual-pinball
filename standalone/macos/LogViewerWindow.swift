// license:GPLv3+
//
// LogViewerWindow.swift — a separate Window scene that tails the
// engine's plog rolling log file (vpinball.log under Application
// Support). Opened from the View menu (⇧⌘L) when the user wants
// more detail than the in-game toasts provide.
//
// Tailing approach (file-based) rather than a live in-process
// appender:
//   * Zero engine changes — the file is already the canonical sink.
//   * Survives engine crashes — historical lines remain readable.
//   * Decouples lifecycle — the window can open before/after a
//     player exists, and works while the playfield is loading.
//
// A 500 ms Timer reads any bytes appended since the last poll.
// On open we read the tail (last ~256 KB) for context. Lines are
// color-coded by level via a lightweight regex; the textual format
// matches ThreadAwareTxtFormatter in src/utils/Logger.cpp.

import SwiftUI
import Foundation


private let kInitialTailBytes: UInt64 = 256 * 1024
private let kPollInterval: TimeInterval = 0.5
private let kMaxLinesInMemory: Int = 5_000


enum LogLevel: String {
    case fatal   = "FATAL"
    case error   = "ERROR"
    case warn    = "WARN"
    case info    = "INFO"
    case debug   = "DEBUG"
    case verbose = "VERBOSE"
    case unknown = ""

    var color: Color {
        switch self {
        case .fatal, .error: return .red
        case .warn:          return .orange
        case .info:          return .primary
        case .debug:         return .secondary
        case .verbose:       return .secondary
        case .unknown:       return .primary
        }
    }
}


struct LogLine: Identifiable {
    let id: Int
    let level: LogLevel
    let text: String
}


final class LogTailer: ObservableObject {
    static let shared = LogTailer()

    @Published private(set) var lines: [LogLine] = []
    @Published private(set) var logPath: String = ""
    @Published private(set) var errorMessage: String?

    private var fileHandle: FileHandle?
    private var nextLineId: Int = 0
    private var carry: String = ""
    private var pollTimer: Timer?
    private var openCount: Int = 0

    private init() {}

    /// Increment open-count and start tailing on the first open. The
    /// matching `release()` stops the timer when the last viewer closes.
    func retain() {
        openCount += 1
        if openCount == 1 {
            start()
        }
    }

    func release() {
        openCount = max(0, openCount - 1)
        if openCount == 0 {
            stop()
        }
    }

    func clear() {
        lines.removeAll(keepingCapacity: true)
    }

    private func start() {
        errorMessage = nil
        let path = Self.resolveLogPath()
        logPath = path

        guard !path.isEmpty else {
            errorMessage = "Log path unavailable (engine not initialised)."
            return
        }

        guard let handle = FileHandle(forReadingAtPath: path) else {
            errorMessage = "Could not open log file at \(path)"
            return
        }
        fileHandle = handle

        // Seek to (size − kInitialTailBytes) so the viewer opens with
        // recent context rather than starting from the top of a 5 MB file.
        let size = (try? handle.seekToEnd()) ?? 0
        let start = size > kInitialTailBytes ? size - kInitialTailBytes : 0
        try? handle.seek(toOffset: start)
        appendAvailable()

        pollTimer = Timer.scheduledTimer(withTimeInterval: kPollInterval, repeats: true) { [weak self] _ in
            self?.appendAvailable()
        }
    }

    private func stop() {
        pollTimer?.invalidate()
        pollTimer = nil
        try? fileHandle?.close()
        fileHandle = nil
        carry = ""
        // Keep `lines` so reopening retains scrollback for the session.
    }

    private func appendAvailable() {
        guard let handle = fileHandle else { return }
        // readToEnd() throws AND returns Optional<Data>, so `try?` here
        // produces Data?? — unwrap both layers before use.
        guard let outer = try? handle.readToEnd(),
              let data = outer,
              !data.isEmpty else { return }
        guard var chunk = String(data: data, encoding: .utf8) else { return }

        if !carry.isEmpty {
            chunk = carry + chunk
            carry = ""
        }

        // Split on \n but hold back any trailing partial line for the
        // next poll — log lines arrive whole, but a poll can land
        // mid-line if the writer hasn't flushed the newline yet.
        var parts = chunk.components(separatedBy: "\n")
        if !chunk.hasSuffix("\n"), let last = parts.popLast() {
            carry = last
        }

        var newLines: [LogLine] = []
        newLines.reserveCapacity(parts.count)
        for raw in parts where !raw.isEmpty {
            newLines.append(LogLine(
                id: nextLineId,
                level: Self.parseLevel(raw),
                text: raw
            ))
            nextLineId += 1
        }
        if newLines.isEmpty { return }

        var combined = lines + newLines
        if combined.count > kMaxLinesInMemory {
            combined.removeFirst(combined.count - kMaxLinesInMemory)
        }
        lines = combined
    }

    private static func parseLevel(_ line: String) -> LogLevel {
        // Format from ThreadAwareTxtFormatter:
        //   2026-05-24 16:54:50.486 ERROR [5077088] [Scope::fn@line] msg
        // The level is the third whitespace-delimited token.
        let parts = line.split(separator: " ", maxSplits: 3, omittingEmptySubsequences: true)
        guard parts.count >= 3 else { return .unknown }
        return LogLevel(rawValue: String(parts[2])) ?? .unknown
    }

    private static func resolveLogPath() -> String {
        var buf = [CChar](repeating: 0, count: 1024)
        let n = vpx_get_log_path(&buf, Int32(buf.count))
        if n <= 0 { return "" }
        return String(cString: buf)
    }
}


/// Singleton model so the View-menu toggle can flip its title between
/// "Show Log" and "Hide Log" reactively, mirroring ControlsHintModel.
final class LogViewerModel: ObservableObject {
    static let shared = LogViewerModel()
    @Published var isOpen: Bool = false
    private init() {}
}


struct LogViewerView: View {
    @ObservedObject private var tailer = LogTailer.shared
    @State private var follow: Bool = true
    @State private var filter: String = ""
    @State private var minLevel: LogLevel = .info

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Divider()
            if let err = tailer.errorMessage {
                Text(err)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                logList
            }
        }
        .frame(minWidth: 640, minHeight: 360)
        .onAppear {
            tailer.retain()
            LogViewerModel.shared.isOpen = true
        }
        .onDisappear {
            tailer.release()
            LogViewerModel.shared.isOpen = false
        }
    }

    private var toolbar: some View {
        HStack(spacing: 12) {
            TextField("Filter", text: $filter)
                .textFieldStyle(.roundedBorder)
                .frame(maxWidth: 240)

            Picker("Level", selection: $minLevel) {
                Text("Verbose").tag(LogLevel.verbose)
                Text("Debug").tag(LogLevel.debug)
                Text("Info").tag(LogLevel.info)
                Text("Warn").tag(LogLevel.warn)
                Text("Error").tag(LogLevel.error)
            }
            .pickerStyle(.menu)
            .frame(maxWidth: 140)

            Toggle("Follow", isOn: $follow)
                .toggleStyle(.checkbox)

            Spacer()

            Button("Clear") { tailer.clear() }
            Button("Reveal in Finder") {
                guard !tailer.logPath.isEmpty else { return }
                NSWorkspace.shared.activateFileViewerSelecting(
                    [URL(fileURLWithPath: tailer.logPath)])
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
    }

    private var visibleLines: [LogLine] {
        let f = filter.lowercased()
        let minRank = rank(minLevel)
        return tailer.lines.filter { line in
            if rank(line.level) < minRank { return false }
            if f.isEmpty { return true }
            return line.text.lowercased().contains(f)
        }
    }

    private var logList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    ForEach(visibleLines) { line in
                        Text(line.text)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(line.level.color)
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 1)
                            .id(line.id)
                    }
                    // Sentinel for scroll-to-bottom.
                    Color.clear.frame(height: 1).id("__tail__")
                }
            }
            .background(Color(NSColor.textBackgroundColor))
            .onChange(of: tailer.lines.count) { _, _ in
                if follow {
                    proxy.scrollTo("__tail__", anchor: .bottom)
                }
            }
            .onAppear {
                proxy.scrollTo("__tail__", anchor: .bottom)
            }
        }
    }

    private func rank(_ l: LogLevel) -> Int {
        switch l {
        case .verbose: return 0
        case .debug:   return 1
        case .info:    return 2
        case .warn:    return 3
        case .error:   return 4
        case .fatal:   return 5
        case .unknown: return 2 // treat unknown as info
        }
    }
}
