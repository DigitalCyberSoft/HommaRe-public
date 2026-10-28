// HallOfFame.swift - "hall_of_fame": the best games, by campaign or by
// scenario. The two books as tabs, the entry chosen from a menu, its table
// below; Clear asks first.

import SwiftUI

struct HallOfFameScreen: Decodable {
    let title: String
    let books: [String]
    let book: Int
    let entries: [String]
    let entry: Int
    let rows: [Row]
    let columns: [String]
    let buttons: [ScreenButton]

    struct Row: Decodable, Hashable {
        let rank: Int
        let name: String
        let badge: String
        /// The badge's picture (a creature's icon), by its path in the paks.
        let badge_icon: String?
        let days: Int
        let score: Int
    }
}

extension HallOfFameScreen: ScreenDocument {
    func view() -> some View { HallOfFameView(screen: self) }
}

struct HallOfFameView: View {
    let screen: HallOfFameScreen
    @EnvironmentObject var store: ScreenStore
    @State private var clearing = false

    var body: some View {
        VStack(spacing: 8) {
            ScreenHeader(title: screen.title) { store.send("press", ["name": "close"]) }
            HStack(spacing: 8) {
                ForEach(Array(screen.books.enumerated()), id: \.offset) { i, name in
                    Button(name) { store.send("book", ["index": i]) }
                        .buttonStyle(PlateButtonStyle(selected: i == screen.book))
                }
                Menu {
                    ForEach(Array(screen.entries.enumerated()), id: \.offset) { i, name in
                        Button {
                            store.send("entry", ["index": i])
                        } label: {
                            if i == screen.entry { Label(name, systemImage: "checkmark") } else { Text(name) }
                        }
                    }
                } label: {
                    HStack {
                        Text(screen.entries.indices.contains(screen.entry) ? screen.entries[screen.entry] : "—")
                            .lineLimit(1)
                        Spacer(minLength: 6)
                        DropIcon()
                    }
                    .font(Theme.body(16, weight: .medium))
                    .foregroundStyle(Theme.text)
                    .padding(.horizontal, 12)
                    .frame(maxWidth: .infinity, minHeight: Theme.touch)
                    .gameField()
                }
                .disabled(screen.entries.isEmpty)
                if let clear = screen.buttons[named: "clear"] {
                    Button(clear.label) { clearing = true }
                        .buttonStyle(PlateButtonStyle())
                        .disabled(!clear.enabled || screen.rows.isEmpty)
                }
            }
            VStack(spacing: 0) {
                header
                ScrollView {
                    LazyVStack(spacing: 4) {
                        ForEach(screen.rows, id: \.self) { row in
                            HStack {
                                Text("\(row.rank)").frame(width: 50, alignment: .center)
                                    .font(Theme.heading(20)).foregroundStyle(Theme.gold)
                                if let icon = row.badge_icon, !icon.isEmpty {
                                    GameImage(ref: icon, placeholder: "shield")
                                        .frame(width: 40, height: 40)
                                        .clipShape(RoundedRectangle(cornerRadius: 6))
                                        .frame(width: 48)
                                }
                                VStack(alignment: .leading, spacing: 1) {
                                    Text(row.name).font(Theme.heading(18)).foregroundStyle(Theme.text)
                                    if !row.badge.isEmpty {
                                        Text(row.badge).font(Theme.body(12)).foregroundStyle(Theme.textDim)
                                    }
                                }
                                .frame(maxWidth: .infinity, alignment: .leading)
                                Text("\(row.days)").frame(width: 80, alignment: .trailing)
                                Text("\(row.score)").frame(width: 100, alignment: .trailing)
                                    .foregroundStyle(Theme.gold)
                            }
                            .font(Theme.body(17, weight: .medium))
                            .foregroundStyle(Theme.text)
                            .padding(.horizontal, 12)
                            .frame(minHeight: 50)
                            .background(RoundedRectangle(cornerRadius: 8).fill(Theme.row))
                        }
                    }
                    .padding(8)
                }
            }
            .panel()
        }
        .padding(.horizontal, 8)
        .padding(.top, 6)
        .padding(.bottom, 4)
        .gameConfirm(isPresented: $clearing, title: store.tr("Очистить результаты?", "Clear the scores?"),
                     message: screen.entries.indices.contains(screen.entry) ? screen.entries[screen.entry] : nil,
                     confirm: store.tr("Очистить", "Clear"), cancel: store.tr("Отмена", "Cancel")) {
            store.send("clear")
        }
    }

    private var header: some View {
        HStack {
            Text(screen.columns[safe: 0] ?? "").frame(width: 50)
            if screen.rows.contains(where: { !($0.badge_icon ?? "").isEmpty }) {
                Text(screen.columns[safe: 4] ?? "").frame(width: 48)
                    .lineLimit(1).minimumScaleFactor(0.6)
            }
            Text(screen.columns[safe: 1] ?? "").frame(maxWidth: .infinity, alignment: .leading)
            Text(screen.columns[safe: 2] ?? "").frame(width: 80, alignment: .trailing)
            Text(screen.columns[safe: 3] ?? "").frame(width: 100, alignment: .trailing)
        }
        .font(Theme.body(13))
        .foregroundStyle(Theme.textDim)
        .padding(.horizontal, 20)
        .padding(.top, 8)
    }
}

extension Array {
    subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil }
}
