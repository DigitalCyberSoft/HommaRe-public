// Question.swift - "question": a game question (the game's message box) as a
// native popup over whatever is behind it, the game's own screen or one of
// ours: its title and text, and OK, or Yes and No.

import SwiftUI

struct QuestionScreen: Decodable {
    let box: UInt32
    let title: String
    let text: String
    let buttons: [ScreenButton]
}

extension QuestionScreen: ScreenDocument {
    func view() -> some View { QuestionView(screen: self) }
}

struct QuestionView: View {
    let screen: QuestionScreen
    @EnvironmentObject var store: ScreenStore

    var body: some View {
        VStack(spacing: 12) {
            if !screen.title.isEmpty {
                Text(screen.title)
                    .font(Theme.heading(24))
                    .foregroundStyle(Theme.gold)
                    .multilineTextAlignment(.center)
            }
            if !screen.text.isEmpty {
                // As tall as the text: the game's questions are short.
                Text(screen.text)
                    .font(Theme.body(17))
                    .foregroundStyle(Theme.text)
                    .multilineTextAlignment(.center)
                    .lineLimit(10)
                    .minimumScaleFactor(0.7)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity)
            }
            HStack(spacing: 10) {
                if let cancel = screen.buttons[named: "cancel"] {
                    Button { store.send("cancel") } label: {
                        Text(cancel.label)
                            .font(Theme.heading(20))
                            .foregroundStyle(Theme.text)
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(PlateButtonStyle())
                }
                if let ok = screen.buttons[named: "ok"] {
                    Button(ok.label) { store.send("ok") }
                        .buttonStyle(PrimaryButtonStyle())
                }
            }
            .padding(.top, 4)
        }
        .padding(20)
        .frame(width: 440)
        .panel()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        // A new question is a new popup.
        .id(screen.box)
    }
}
