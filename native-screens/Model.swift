// Model.swift - the documents the game sends (native/native_screens.c writes
// them): one struct per screen, the parts they share. Every text is the
// game's own, in its language, with its markup taken out.

import Foundation
import SwiftUI

/// One of the screen's own buttons: its message name, the game's label for
/// it and whether the game has it enabled.
struct ScreenButton: Decodable, Hashable {
    let name: String
    let label: String
    let enabled: Bool
}

/// A line of a map's facts: "Игроков:" "4 (против всех)".
struct Fact: Decodable, Hashable {
    let label: String
    let value: String
    init(from decoder: Decoder) throws {
        // Written as a pair: ["Игроков:", "4 (против всех)"].
        var c = try decoder.unkeyedContainer()
        label = try c.decode(String.self)
        value = try c.decode(String.self)
    }
    init(label: String, value: String) {
        self.label = label
        self.value = value
    }
}

/// The chosen map as the game describes it.
struct MapInfo: Decodable, Hashable {
    let name: String
    let facts: [Fact]
    let descriptionTitle: String?
    let description: String?
    /// A PNG of the map's picture the game wrote, when it has one.
    let preview: String?

    enum CodingKeys: String, CodingKey {
        case name, facts, description, preview
        case descriptionTitle = "description_title"
    }
}

extension Array where Element == ScreenButton {
    subscript(named name: String) -> ScreenButton? { first { $0.name == name } }
}

// MARK: - custom_create

struct CreateScreen: Decodable {
    let title: String
    /// The list's column names, in the game's order: name, goal, players, size.
    let columns: [String]
    /// The column the list is sorted by (an index into columns) and its way.
    let sort: Int
    let descending: Bool
    let maps: [MapRow]
    /// The chosen row, -1 for none.
    let selected: Int
    let info: MapInfo?
    let options: [ScreenOption]
    /// On-off settings ("mp_maps": the multiplayer maps listed too).
    let toggles: [ScreenToggle]?
    /// A network game's own settings (server name, password, switches).
    let fields: [FormField]?
    /// What the game is busy with (creating the game), when it is.
    let busy: String?
    let buttons: [ScreenButton]
}

struct ScreenToggle: Decodable, Hashable, Identifiable {
    let id: String
    let label: String
    let on: Bool
}

struct MapRow: Decodable, Hashable {
    let name: String
    let goal: String
    let players: String
    let size: String
}

/// A setting with a list of values (the turn's time limit, the difficulty).
struct ScreenOption: Decodable, Hashable, Identifiable {
    let id: String
    let label: String
    /// The values' names when the game has them all; else only `value`.
    let values: [String]?
    let value: String
    let index: Int
    let count: Int
    let enabled: Bool
}

// MARK: - custom_players

struct PlayersScreen: Decodable {
    let title: String
    let players: [PlayerRow]
    let info: MapInfo?
    let buttons: [ScreenButton]
    /// A network game's chat (Lan.swift).
    let chat: ChatState?
    /// A network game's status while it connects or starts (Lan.swift).
    let busy: String?
}

struct PlayerRow: Decodable, Hashable, Identifiable {
    var id: Int { slot }
    let slot: Int
    /// "красный игрок"
    let label: String
    /// Hot seat: the name typed for this player (the game keeps it in the row).
    let name: String?
    /// Its colour's index (Theme.players).
    let colour: Int
    let cells: [PlayerCell]
}

/// One of a player's settings. `field` is the game's own number for it:
/// 1 who plays (a person, the computer, nobody), 2 colour, 3 faction, 4 hero,
/// 5 starting bonus.
struct PlayerCell: Decodable, Hashable, Identifiable {
    var id: Int { field }
    let field: Int
    let title: String
    let value: String
    let enabled: Bool
    /// Every value it can take, in the game's order, when the game listed
    /// them; then `index` is the current one, and a choice is "set".
    let values: [String]?
    let index: Int?
    /// The game's picture for the value (a faction's crest, a hero's
    /// portrait), by its path in the paks; and one per value of `values`.
    let icon: String?
    let icons: [String]?
    /// A line under the value (a hero's specialisation).
    let detail: String?
}

extension CreateScreen: ScreenDocument {
    func view() -> some View { CreateScreenView(screen: self) }
}

extension PlayersScreen: ScreenDocument {
    func view() -> some View { PlayersScreenView(screen: self) }
}
