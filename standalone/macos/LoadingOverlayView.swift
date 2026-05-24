// license:GPLv3+
//
// LoadingOverlayView.swift — translucent splash shown over the Metal
// playfield while the app starts up and while the engine loads a table.
// Replaces "dark empty window for ~10 seconds with no feedback" with
// a visible status + progress bar.
//
// Lifecycle is bridge-driven:
//   * SwiftUI sets the initial state ("Opening VPinballX…") at app
//     launch.
//   * After the user picks a table, SwiftUI sets "Loading <name>…".
//   * vpx_run starts and the engine's ProgressDialog::SetProgress calls
//     push richer text + percent (Initialising Visuals 10 %, Loading
//     Textures 50 %, Prerendering Static Parts 70-100 %, etc.).
//   * SwiftUI clears the active flag once progress reaches 100 % AND
//     a short settle delay passes so the playfield has time to draw
//     its first frame underneath.

import SwiftUI
import Foundation


private let kPollInterval: TimeInterval = 0.1
private let kHideDelayAfter100: TimeInterval = 0.5
private let kWarmupTimeoutSeconds: TimeInterval = 30


final class LoadingModel: ObservableObject {
    static let shared = LoadingModel()

    @Published private(set) var isActive: Bool = false
    @Published private(set) var text: String = ""
    @Published private(set) var percent: Int = -1

    private var timer: Timer?
    private var hidePendingDeadline: Date?
    private var warmupStartedAt: Date?
    private var wasWarmingLastTick: Bool = false

    private init() {}

    /// Show the overlay with an initial message. Idempotent — calling
    /// multiple times just replaces the text.
    func begin(_ message: String) {
        text = message
        percent = -1
        isActive = true
        hidePendingDeadline = nil
        vpx_loading_set_active(1)
        vpx_loading_set(message, -1)
        startTimerIfNeeded()
    }

    func end() {
        isActive = false
        text = ""
        percent = -1
        hidePendingDeadline = nil
        vpx_loading_set_active(0)
        stopTimer()
    }

    private func startTimerIfNeeded() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: kPollInterval, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    private func stopTimer() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        var buf = [CChar](repeating: 0, count: 256)
        var pct: Int32 = -1
        let active = vpx_loading_get(&buf, Int32(buf.count), &pct) != 0

        if !active {
            end()
            return
        }

        let newText = String(cString: buf)
        if !newText.isEmpty && newText != text {
            text = newText
        }
        let newPct = Int(pct)
        if newPct != percent {
            percent = newPct
        }

        let isWarming = text == "Warming up emulator…"
        let audioCount = Int(vpx_loading_get_audio_count())

        if isWarming {
            hidePendingDeadline = nil
            if !wasWarmingLastTick {
                warmupStartedAt = Date()
            }
            // First audio buffer = emulator alive. Bridge already
            // dropped its sticky flag in vpx_loading_audio_arrived;
            // the next active-check above will hide on its own —
            // but call end() here so the user sees the snap close
            // promptly without an extra 100 ms poll.
            if audioCount > 0 {
                end()
            } else if let started = warmupStartedAt,
                      Date().timeIntervalSince(started) > kWarmupTimeoutSeconds {
                // Trap door: no audio ever arrived (silent table /
                // mis-configured ROM). Give up and let the user in.
                end()
            }
        } else if percent >= 100 {
            // Engine reported done. If audio is already flowing
            // (PinmameRun fired early enough that buffers landed
            // before prerender finished), hide on the short delay.
            // Otherwise switch to warmup mode and wait for audio.
            if audioCount > 0 {
                if hidePendingDeadline == nil {
                    hidePendingDeadline = Date().addingTimeInterval(kHideDelayAfter100)
                } else if Date() >= hidePendingDeadline! {
                    end()
                }
            } else {
                vpx_loading_emulator_starting()
                // The next tick will see the new text + sticky flag.
            }
        }

        wasWarmingLastTick = isWarming
    }
}


struct LoadingOverlayView: View {
    @ObservedObject private var model = LoadingModel.shared

    var body: some View {
        Group {
            if model.isActive {
                card
                    .transition(.opacity.combined(with: .scale(scale: 0.96)))
            }
        }
        .animation(.easeInOut(duration: 0.25), value: model.isActive)
        .allowsHitTesting(false)
    }

    private var card: some View {
        VStack(spacing: 14) {
            ProgressView()
                .controlSize(.large)
                .tint(.white)

            Text(model.text.isEmpty ? "Loading…" : model.text)
                .font(.system(size: 14, weight: .medium))
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
                .lineLimit(2)
                .frame(minWidth: 260)

            if model.percent >= 0 {
                ProgressView(value: Double(model.percent), total: 100)
                    .progressViewStyle(.linear)
                    .tint(.white)
                    .frame(width: 220)
                Text("\(model.percent)%")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.white.opacity(0.7))
            }
        }
        .padding(28)
        .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .strokeBorder(Color.white.opacity(0.18), lineWidth: 0.5)
        )
        .shadow(color: .black.opacity(0.4), radius: 16, y: 4)
    }
}
