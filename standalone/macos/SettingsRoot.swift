// license:GPLv3+
//
// SettingsRoot.swift — the SwiftUI Settings scene root.
//
// SwiftUI's `Settings { … }` scene is the idiomatic Cmd+, surface: the
// system menu item is wired automatically, the window remembers its
// position, and the OS treats it as a secondary scene that doesn't
// interfere with the playfield WindowGroup.
//
// For now we ship one tab (Display). The TabView structure is ready
// for additional tabs (Audio, Graphics, Input, etc.) in future slices.

import SwiftUI


struct SettingsRoot: View {
    var body: some View {
        TabView {
            DisplaySettingsView()
                .tabItem {
                    Label("Display", systemImage: "display")
                }
                .tag(SettingsTab.display)

            ViewSettingsView()
                .tabItem {
                    Label("View", systemImage: "camera.viewfinder")
                }
                .tag(SettingsTab.view)
        }
        .frame(minWidth: 480, minHeight: 360)
    }
}


private enum SettingsTab: Hashable {
    case display
    case view
}
