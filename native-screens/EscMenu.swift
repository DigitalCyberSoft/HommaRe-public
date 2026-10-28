// EscMenu.swift - "esc_menu": the gear button's menu on the adventure map
// and in battle, over the map or the battle the game keeps drawn: its items
// two to a row, and Return to the game below them. Items that ask first
// (restart, main menu, surrender, quit) get the game's question as the
// native popup.

import SwiftUI

struct EscMenuScreen: Decodable {
    let title: String
    let buttons: [ScreenButton]
}

extension EscMenuScreen: ScreenDocument {
    func view() -> some View { EscMenuView(screen: self) }
}

struct EscMenuView: View {
    let screen: EscMenuScreen
    @EnvironmentObject var store: ScreenStore

    private var items: [ScreenButton] { screen.buttons.filter { $0.name != "return_to_game" } }

    var body: some View {
        VStack(spacing: 8) {
            LazyVGrid(columns: [GridItem(.flexible(), spacing: 8), GridItem(.flexible(), spacing: 8)], spacing: 8) {
                ForEach(items, id: \.name) { b in
                    Button(b.label) { store.send("press", ["name": b.name]) }
                        .buttonStyle(MenuButtonStyle())
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                        .disabled(!b.enabled)
                }
            }
            if let back = screen.buttons[named: "return_to_game"] {
                Button(back.label) { store.send("press", ["name": back.name]) }
                    .buttonStyle(PrimaryButtonStyle())
                    .disabled(!back.enabled)
                    .padding(.top, 4)
            }
        }
        .frame(width: 505)
        .ornate(title: screen.title)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
