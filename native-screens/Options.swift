// Options.swift - "options": the game's options, a tab per category (Game;
// Video and sound) with the category's rows as the game built them: section
// headers, switches, and choices (the game's combo boxes and sliders; a few
// values as segments, more as a menu). Apply stores the tab; Defaults asks
// first; Back leaves without applying.

import SwiftUI

struct OptionsScreen: Decodable {
    let title: String
    let tabs: [String]
    let tab: Int
    let rows: [Row]
    let buttons: [ScreenButton]

    struct Row: Decodable, Identifiable {
        var id: Int { row }
        let row: Int
        let kind: String
        let label: String
        let enabled: Bool
        let on: Bool?
        let index: Int?
        let values: [String]?
    }
}

extension OptionsScreen: ScreenDocument {
    func view() -> some View { OptionsView(screen: self) }
}

struct OptionsView: View {
    let screen: OptionsScreen
    @EnvironmentObject var store: ScreenStore
    @State private var resetting = false
    /// Where each tab was scrolled to (the row at its top): a tab opens at its
    /// top the first time, and where it was left after that.
    @State private var scrolled: [Int: Int] = [:]

    var body: some View {
        VStack(spacing: 8) {
            ScreenHeader(title: screen.title) { store.send("back") }
            HStack(alignment: .top, spacing: 10) {
                VStack(spacing: 8) {
                    ForEach(Array(screen.tabs.enumerated()), id: \.offset) { i, name in
                        Button(name) { store.send("tab", ["index": i]) }
                            .buttonStyle(MenuButtonStyle(selected: i == screen.tab))
                    }
                    Spacer(minLength: 0)
                }
                .frame(width: 220)
                VStack(spacing: 8) {
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 6) {
                            ForEach(screen.rows) { row in
                                OptionRow(row: row)
                            }
                        }
                        .scrollTargetLayout()
                        .padding(10)
                    }
                    .scrollPosition(id: Binding(get: { scrolled[screen.tab] },
                                                set: { scrolled[screen.tab] = $0 }),
                                    anchor: .top)
                    // A scroll view of its own per tab: none carries another's place.
                    .id(screen.tab)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .panel()
                    HStack(spacing: 8) {
                        if let reset = screen.buttons[named: "default"] {
                            Button(reset.label) { resetting = true }
                                .buttonStyle(PlateButtonStyle())
                                .disabled(!reset.enabled)
                                .frame(maxWidth: 200)
                        }
                        if let accept = screen.buttons[named: "accept"] {
                            Button(accept.label) { store.send("accept") }
                                .buttonStyle(PrimaryButtonStyle())
                                .disabled(!accept.enabled)
                        }
                    }
                }
            }
        }
        .padding(.horizontal, 8)
        .padding(.top, 6)
        .padding(.bottom, 4)
        .gameConfirm(isPresented: $resetting, title: store.tr("Вернуть значения по умолчанию?", "Restore the defaults?"),
                     message: screen.tabs.indices.contains(screen.tab) ? screen.tabs[screen.tab] : nil,
                     confirm: store.tr("Вернуть", "Restore"), cancel: store.tr("Отмена", "Cancel")) {
            store.send("default")
        }
    }
}

/// One of a category's rows.
struct OptionRow: View {
    let row: OptionsScreen.Row
    @EnvironmentObject var store: ScreenStore

    var body: some View {
        switch row.kind {
        case "header":
            Text(row.label)
                .font(Theme.heading(19))
                .foregroundStyle(Theme.gold)
                .padding(.top, 6)
        case "toggle":
            SwitchRow(label: row.label, on: row.on ?? false) { on in
                store.send("set", ["row": row.row, "value": on ? 1 : 0])
            }
            .disabled(!row.enabled)
        default:
            choice
        }
    }

    @ViewBuilder private var choice: some View {
        let values = row.values ?? []
        let index = row.index ?? -1
        HStack(spacing: 8) {
            Text(row.label)
                .font(Theme.body(16))
                .foregroundStyle(Theme.text)
                .lineLimit(2)
            Spacer(minLength: 8)
            if values.count <= 4 && values.allSatisfy({ $0.count <= 12 }) {
                HStack(spacing: 4) {
                    ForEach(Array(values.enumerated()), id: \.offset) { i, name in
                        Button(name) { store.send("set", ["row": row.row, "value": i]) }
                            .buttonStyle(PlateButtonStyle(selected: i == index))
                    }
                }
            } else {
                Menu {
                    ForEach(Array(values.enumerated()), id: \.offset) { i, name in
                        Button {
                            store.send("set", ["row": row.row, "value": i])
                        } label: {
                            if i == index { Label(name, systemImage: "checkmark") } else { Text(name) }
                        }
                    }
                } label: {
                    HStack(spacing: 6) {
                        Text(values.indices.contains(index) ? values[index] : "—")
                        DropIcon()
                    }
                    .font(Theme.body(16, weight: .medium))
                    .foregroundStyle(Theme.text)
                    .padding(.horizontal, 12)
                    .frame(minWidth: 140, minHeight: Theme.touch)
                    .gameField()
                }
                .menuOrder(.fixed)
            }
        }
        .frame(minHeight: Theme.touch)
        .disabled(!row.enabled)
        .opacity(row.enabled ? 1 : 0.45)
    }
}
