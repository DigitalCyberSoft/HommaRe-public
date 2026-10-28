// Lan.swift - the LAN games' screens: the server list ("lan_lobby", the
// standard and the duel lobby alike), a settings form ("form": the duel's
// "create a game" dialog and the like) and the chat both lobbies and the
// players screens carry (ChatPanel). The game keeps everything (the games it
// hears of, the chat log, who is in); the views show it and send the
// screen's own commands.

import SwiftUI

// MARK: - The chat

/// The chat log the game keeps and whether a line may be sent now.
struct ChatState: Decodable, Hashable {
    let lines: [String]
    let enabled: Bool
    /// The input's placeholder, in the game's language.
    let hint: String?
}

/// The log, newest at the bottom, and a line to type into.
struct ChatPanel: View {
    let chat: ChatState
    var compact = false
    @EnvironmentObject var store: ScreenStore
    @State private var draft = ""
    @FocusState private var typing: Bool

    var body: some View {
        VStack(spacing: 6) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 3) {
                        ForEach(Array(chat.lines.enumerated()), id: \.offset) { i, line in
                            Text(line)
                                .font(Theme.body(compact ? 13 : 14))
                                .foregroundStyle(Theme.text)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(i)
                        }
                    }
                    .padding(8)
                }
                .onAppear { proxy.scrollTo(chat.lines.count - 1, anchor: .bottom) }
                .onChange(of: chat.lines.count) { proxy.scrollTo(chat.lines.count - 1, anchor: .bottom) }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: 8).fill(Theme.well))
            if chat.enabled {
                HStack(spacing: 6) {
                    TextField(chat.hint ?? store.tr("Сообщение", "Message"), text: $draft)
                        .font(Theme.body(15))
                        .foregroundStyle(Theme.text)
                        .padding(.horizontal, 10)
                        .frame(minHeight: 40)
                        .gameField()
                        .focused($typing)
                        .submitLabel(.send)
                        .onSubmit(send)
                    Button(action: send) {
                        Image(systemName: "paperplane.fill").font(.system(size: 16))
                            .frame(width: 44, height: 40)
                    }
                    .buttonStyle(PlateButtonStyle())
                    .disabled(draft.trimmingCharacters(in: .whitespaces).isEmpty)
                    .accessibilityLabel(store.tr("Отправить", "Send"))
                }
            }
        }
    }

    private func send() {
        let line = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !line.isEmpty else { return }
        store.send("chat", ["text": line])
        draft = ""
    }
}

/// The game at work (joining, creating): its words over everything, no touch through.
struct BusyOverlay: View {
    let text: String
    var body: some View {
        ZStack {
            Color.black.opacity(0.45).ignoresSafeArea()
            VStack(spacing: 12) {
                ProgressView().tint(Theme.gold).controlSize(.large)
                Text(text).font(Theme.heading(22)).foregroundStyle(Theme.gold)
            }
            .padding(24)
            .panel()
        }
        .contentShape(Rectangle())
        .onTapGesture {}
    }
}

// MARK: - lan_lobby

struct LanLobbyScreen: Decodable {
    let title: String
    /// The games list's column names, in the game's order.
    let columns: [String]
    let games: [Game]
    let selected: Int
    /// The list's filters ("Запущенные игры", "Заполненные игры").
    let filters: [Toggle]?
    /// Who is in the lobby.
    let players_title: String?
    let players: [String]
    let chat: ChatState?
    /// The player's name, as the game holds it.
    let name: String?
    /// What the game is busy with (joining a game), when it is.
    let busy: String?
    let buttons: [ScreenButton]

    struct Game: Decodable, Hashable {
        let cells: [String]
        /// Under way already, or closed by a password.
        let started: Bool?
        let locked: Bool?
    }

    struct Toggle: Decodable, Hashable {
        let id: Int
        let label: String
        let on: Bool
    }
}

extension LanLobbyScreen: ScreenDocument {
    func view() -> some View { LanLobbyView(screen: self) }
}

struct LanLobbyView: View {
    let screen: LanLobbyScreen
    @EnvironmentObject var store: ScreenStore
    @State private var pending: Int?
    @State private var tab = 0 // the right column: 0 players, 1 chat

    private var selected: Int { pending ?? screen.selected }

    var body: some View {
        VStack(spacing: 8) {
            ScreenHeader(title: screen.title) { store.send("press", ["name": "back"]) }
            GeometryReader { geo in
                HStack(alignment: .top, spacing: 10) {
                    gamesPanel
                        .frame(width: geo.size.width * 0.6)
                    VStack(spacing: 8) {
                        sidePanel
                        actions
                    }
                }
            }
        }
        .padding(.horizontal, 8)
        .padding(.top, 6)
        .padding(.bottom, 4)
        .onChange(of: screen.selected) { pending = nil }
        .overlay { if let busy = screen.busy { BusyOverlay(text: busy) } }
    }

    private var gamesPanel: some View {
        VStack(spacing: 6) {
            ScrollView {
                LazyVStack(spacing: 6) {
                    if screen.games.isEmpty {
                        VStack(spacing: 8) {
                            ProgressView().tint(Theme.gold)
                            Text(store.tr("Ищем игры в этой сети…", "Looking for games on this network…"))
                                .font(Theme.body(15))
                                .foregroundStyle(Theme.textDim)
                        }
                        .frame(maxWidth: .infinity, minHeight: 140)
                    }
                    ForEach(Array(screen.games.enumerated()), id: \.offset) { i, game in
                        gameRow(game, selected: i == selected)
                            .contentShape(Rectangle())
                            .onTapGesture {
                                guard i != selected else { return }
                                pending = i
                                store.send("select", ["index": i])
                            }
                    }
                }
                .padding(.vertical, 2)
            }
            ForEach(screen.filters ?? [], id: \.id) { f in
                SwitchRow(label: f.label, on: f.on, size: 14) { _ in store.send("filter", ["id": f.id]) }
                .padding(.horizontal, 6)
            }
        }
        .padding(8)
        .panel()
    }

    /// A game: its name large, the other columns small beneath, labelled.
    private func gameRow(_ game: LanLobbyScreen.Game, selected: Bool) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                if game.locked == true {
                    Image(systemName: "lock.fill").font(.system(size: 13)).foregroundStyle(Theme.goldDim)
                }
                Text(game.cells.first ?? "")
                    .font(Theme.heading(19))
                    .foregroundStyle(selected ? Theme.gold : Theme.text)
                    .lineLimit(1)
                if game.started == true {
                    Text(store.tr("идёт", "started"))
                        .font(Theme.body(12, weight: .medium))
                        .foregroundStyle(Theme.accentText)
                        .padding(.horizontal, 6)
                        .background(Capsule().fill(Theme.goldDim))
                }
                Spacer(minLength: 0)
            }
            Text(game.cells.dropFirst().enumerated().compactMap { i, v in
                v.isEmpty ? nil : "\(screen.columns[safe: i + 1] ?? ""): \(v)"
            }.joined(separator: "  ·  "))
                .font(Theme.body(13))
                .foregroundStyle(Theme.textDim)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity, minHeight: 52, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 8).fill(selected ? Theme.rowSelected : Theme.row))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(selected ? Theme.gold : .clear, lineWidth: 1.5))
    }

    /// Who is here and the chat, one at a time.
    private var sidePanel: some View {
        VStack(spacing: 6) {
            if screen.chat != nil {
                Picker("", selection: $tab) {
                    Text(screen.players_title ?? store.tr("Игроки", "Players")).tag(0)
                    Text(store.tr("Чат", "Chat")).tag(1)
                }
                .pickerStyle(.segmented)
            }
            if tab == 1, let chat = screen.chat {
                ChatPanel(chat: chat, compact: true)
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(screen.players.enumerated()), id: \.offset) { _, p in
                            Label(p, systemImage: "person.fill")
                                .font(Theme.body(15))
                                .foregroundStyle(Theme.text)
                                .frame(maxWidth: .infinity, minHeight: 32, alignment: .leading)
                        }
                    }
                    .padding(8)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.well))
            }
        }
        .padding(8)
        .frame(maxHeight: .infinity)
        .panel()
    }

    /// The game's buttons: join first (gold), the rest as plates.
    private var actions: some View {
        let others = screen.buttons.filter { $0.name != "join" && $0.name != "back" }
        return VStack(spacing: 6) {
            HStack(spacing: 6) {
                ForEach(others, id: \.name) { b in
                    Button(b.label) { store.send("press", ["name": b.name]) }
                        .buttonStyle(PlateButtonStyle())
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                        .disabled(!b.enabled)
                }
            }
            if let join = screen.buttons[named: "join"] {
                Button(join.label) { store.send("press", ["name": "join"]) }
                    .buttonStyle(PrimaryButtonStyle())
                    .disabled(!join.enabled || selected < 0)
            }
        }
    }
}

// MARK: - form

/// A dialog of settings: text fields, switches, choices; its buttons below.
struct FormScreen: Decodable {
    let title: String
    let fields: [FormField]
    let buttons: [ScreenButton]
}

struct FormField: Decodable, Hashable, Identifiable {
    let id: Int
    /// "text", "secret" (a password), "toggle" or "choice".
    let kind: String
    let label: String
    let enabled: Bool
    let text: String?
    let on: Bool?
    let index: Int?
    let values: [String]?
}

extension FormScreen: ScreenDocument {
    func view() -> some View { FormView(screen: self) }
}

struct FormView: View {
    let screen: FormScreen
    @EnvironmentObject var store: ScreenStore

    var body: some View {
        VStack(spacing: 10) {
            Text(screen.title).font(Theme.heading(24)).foregroundStyle(Theme.gold)
            ScrollView {
                VStack(spacing: 6) {
                    ForEach(screen.fields) { f in FormFieldRow(field: f) }
                }
            }
            .scrollBounceBehavior(.basedOnSize)
            HStack(spacing: 10) {
                ForEach(screen.buttons.filter { $0.name != "ok" }, id: \.name) { b in
                    Button { store.send("press", ["name": b.name]) } label: {
                        Text(b.label).frame(maxWidth: .infinity)
                    }
                    .buttonStyle(PlateButtonStyle())
                    .disabled(!b.enabled)
                }
                if let ok = screen.buttons[named: "ok"] {
                    Button(ok.label) { store.send("press", ["name": "ok"]) }
                        .buttonStyle(PrimaryButtonStyle())
                        .disabled(!ok.enabled)
                }
            }
        }
        .padding(16)
        .frame(width: 480)
        .panel()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(.vertical, 8)
    }
}

/// One setting of a form; a text is sent when its typing ends.
struct FormFieldRow: View {
    let field: FormField
    @EnvironmentObject var store: ScreenStore
    @State private var draft: String?

    var body: some View {
        Group {
            switch field.kind {
            case "toggle":
                SwitchRow(label: field.label, on: field.on ?? false) {
                    store.send("set", ["id": field.id, "value": $0 ? 1 : 0])
                }
            case "choice":
                HStack {
                    Text(field.label).font(Theme.body(16)).foregroundStyle(Theme.text).lineLimit(1)
                    Spacer(minLength: 8)
                    Menu {
                        ForEach(Array((field.values ?? []).enumerated()), id: \.offset) { i, v in
                            Button {
                                store.send("set", ["id": field.id, "value": i])
                            } label: {
                                if i == field.index { Label(v, systemImage: "checkmark") } else { Text(v) }
                            }
                        }
                    } label: {
                        HStack(spacing: 4) {
                            Text(field.values?[safe: field.index ?? -1] ?? "—").lineLimit(1)
                            DropIcon()
                        }
                        .font(Theme.body(16, weight: .medium))
                        .foregroundStyle(Theme.text)
                        .frame(minHeight: Theme.touch)
                    }
                    .menuOrder(.fixed)
                }
            default:
                VStack(alignment: .leading, spacing: 3) {
                    Text(field.label).font(Theme.body(13)).foregroundStyle(Theme.textDim)
                    let binding = Binding(get: { draft ?? field.text ?? "" }, set: { draft = $0 })
                    Group {
                        if field.kind == "secret" {
                            SecureField("", text: binding)
                        } else {
                            TextField("", text: binding)
                        }
                    }
                    .font(Theme.body(17))
                    .foregroundStyle(Theme.text)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .padding(.horizontal, 10)
                    .frame(minHeight: Theme.touch)
                    .gameField()
                    .onSubmit(commit)
                    .onChange(of: draft) { commit() }
                }
            }
        }
        .disabled(!field.enabled)
        .opacity(field.enabled ? 1 : 0.45)
    }

    private func commit() {
        guard let draft, draft != field.text else { return }
        store.send("text", ["id": field.id, "text": draft])
    }
}
