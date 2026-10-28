// GameSheets.swift - the native screens' own pop-ups (plan C): a sheet of
// the game's parchment over the screen, and a question with the game's
// buttons, instead of the system's sheets, confirmation dialogs and alerts.
// A tap beside the sheet closes it, as the round Back in its corner does.

import SwiftUI

/// A sheet over the screen: its title, the round Back, its content.
struct GameSheet<Content: View>: View {
    let title: String
    var width: CGFloat = 520
    let close: () -> Void
    @ViewBuilder var content: () -> Content

    var body: some View {
        ZStack {
            Color.black.opacity(0.4)
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture(perform: close)
            VStack(spacing: 8) {
                HStack(spacing: 10) {
                    Text(title)
                        .font(Theme.heading(22))
                        .foregroundStyle(Theme.gold)
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                    Spacer(minLength: 8)
                    BackButton(action: close)
                }
                content()
            }
            .frame(maxWidth: width)
            .panel()
            .padding(.vertical, 10)
        }
        .transition(.opacity)
    }
}

/// A question: its text, Cancel (khaki) and the action (gold).
struct GameConfirm: View {
    let title: String
    let message: String?
    let confirm: String
    let cancel: String
    let action: () -> Void
    let close: () -> Void

    var body: some View {
        GameSheet(title: title, width: 440, close: close) {
            VStack(spacing: 12) {
                if let message, !message.isEmpty {
                    Text(message)
                        .font(Theme.body(17))
                        .foregroundStyle(Theme.text)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: .infinity)
                }
                HStack(spacing: 10) {
                    Button(action: close) { Text(cancel).frame(maxWidth: .infinity) }
                        .buttonStyle(PlateButtonStyle())
                    Button {
                        action()
                        close()
                    } label: {
                        Text(confirm)
                    }
                    .buttonStyle(PrimaryButtonStyle())
                }
            }
        }
    }
}

extension View {
    /// `content` in a game sheet while `isPresented`.
    func gameSheet<C: View>(isPresented: Binding<Bool>, title: String, width: CGFloat = 520,
                            @ViewBuilder content: @escaping () -> C) -> some View {
        overlay {
            if isPresented.wrappedValue {
                GameSheet(title: title, width: width, close: { isPresented.wrappedValue = false }, content: content)
            }
        }
        .animation(.easeOut(duration: 0.15), value: isPresented.wrappedValue)
    }

    /// `content` for the item in a game sheet while there is one.
    func gameSheet<Item: Identifiable, C: View>(item: Binding<Item?>, title: @escaping (Item) -> String,
                                                width: CGFloat = 520,
                                                @ViewBuilder content: @escaping (Item) -> C) -> some View {
        overlay {
            if let it = item.wrappedValue {
                GameSheet(title: title(it), width: width, close: { item.wrappedValue = nil }) { content(it) }
            }
        }
        .animation(.easeOut(duration: 0.15), value: item.wrappedValue != nil)
    }

    /// A game question while `isPresented`; `action` on its gold button.
    func gameConfirm(isPresented: Binding<Bool>, title: String, message: String? = nil, confirm: String,
                     cancel: String, action: @escaping () -> Void) -> some View {
        overlay {
            if isPresented.wrappedValue {
                GameConfirm(title: title, message: message, confirm: confirm, cancel: cancel, action: action,
                            close: { isPresented.wrappedValue = false })
            }
        }
        .animation(.easeOut(duration: 0.15), value: isPresented.wrappedValue)
    }
}
