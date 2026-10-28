// GameFont.swift - the game's own lettering as fonts for the native screens.
//
// The game has no font files: its interface type is letter pictures
// (UI/Fonts/<name>_<px>.(Texture).dds) with a glyph table (Bin/Fonts/<uid>:
// each letter's place in the picture, its offset and advance), named by
// UI/Fonts/<name>_<px>.(Font).xdb. Default is the italic of the game's text,
// Header the same italic bold. Each is built here, on the player's device and
// from their install (it may not ship with the app), into a TrueType font:
// the largest size's letters (40 px) are smoothed 4x, their ink traced into
// outlines and simplified, the game's metrics kept. The result is kept in the
// app's Caches and registered for the process as "HeroesV-Italic" and
// "HeroesV-BoldItalic" (research notes game-font, the session that worked it out
// with potrace; this is a dependency-free port of it).

import CoreGraphics
import CoreText
import Foundation

enum GameFont {
    static let regular = "HeroesV-Italic"
    static let bold = "HeroesV-BoldItalic"

    /// Builds or loads both fonts and registers them; true when they are.
    @discardableResult
    static func prepare() -> Bool {
        var ok = true
        for (xdb, ps, style, isBold) in [("UI/Fonts/Default_40.(Font).xdb", regular, "Italic", false),
                                          ("UI/Fonts/Header_40.(Font).xdb", bold, "Bold Italic", true)] {
            guard let data = build(xdb: xdb, ps: ps, style: style, bold: isBold),
                  let provider = CGDataProvider(data: data as CFData), let font = CGFont(provider) else {
                ok = false
                continue
            }
            var error: Unmanaged<CFError>?
            if !CTFontManagerRegisterGraphicsFont(font, &error) {
                // Already registered (a second show in one process) is fine.
                ok = ok && (font.postScriptName as String?) == ps
            }
        }
        return ok
    }

    /// The font's bytes: from the Caches when this install's table built it
    /// before, else built now and kept.
    private static func build(xdb: String, ps: String, style: String, bold: Bool) -> Data? {
        guard let desc = GameArt.shared.file(xdb).flatMap({ String(data: $0, encoding: .utf8) }),
              let uid = match(desc, "<uid>([^<]+)</uid>"),
              let tex = match(desc, "<Texture href=\"([^\"#]+)"),
              let table = GameArt.shared.file("Bin/Fonts/" + uid) else { return nil }
        let folder = (xdb as NSString).deletingLastPathComponent
        let key = String(format: "%@-%08x.ttf", ps, fnv(table))
        let dir = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("HommaRe-fonts")
        let file = dir.appendingPathComponent(key)
        if let cached = try? Data(contentsOf: file) { return cached }
        guard let atlas = GameArt.shared.image(folder + "/" + tex)?.cgImage,
              let (px, w, h) = NineCut.rgba(atlas),
              let glyphs = GlyphTable(table) else { return nil }
        let size = 40 // the em: the game's size N reads as SwiftUI's size N
        let ttf = FontWriter.build(glyphs: glyphs, alpha: { x, y in
            x < 0 || y < 0 || x >= w || y >= h ? 0 : px[(y * w + x) * 4 + 3]
        }, emPx: size, ps: ps, style: style, bold: bold)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try? ttf.write(to: file)
        return ttf
    }

    private static func match(_ s: String, _ pattern: String) -> String? {
        guard let re = try? NSRegularExpression(pattern: pattern),
              let m = re.firstMatch(in: s, range: NSRange(s.startIndex..., in: s)),
              let r = Range(m.range(at: 1), in: s) else { return nil }
        return String(s[r])
    }

    private static func fnv(_ d: Data) -> UInt32 {
        var h: UInt32 = 2_166_136_261
        for b in d { h = (h ^ UInt32(b)) &* 16_777_619 }
        return h
    }
}

/// A Bin/Fonts table (Nival's binary): line height and descent, then each
/// character and its rectangle in the picture (x1, y1, x2, y2), its offset
/// and the rest of its advance.
struct GlyphTable {
    var lineHeight = 0, descent = 0
    var glyphs: [(char: UInt16, x1: Int, y1: Int, x2: Int, y2: Int, a: Int, b: Int)] = []

    init?(_ data: Data) {
        let d = [UInt8](data)
        func i32(_ p: Int) -> Int { Int(Int32(bitPattern: UInt32(d[p]) | UInt32(d[p + 1]) << 8 | UInt32(d[p + 2]) << 16 | UInt32(d[p + 3]) << 24)) }
        func tag(_ t: UInt8) -> Int? {
            for p in 0..<min(0x60, d.count - 6) where d[p] == t && d[p + 1] == 0x08 { return i32(p + 2) }
            return nil
        }
        guard let lh = tag(0x0a), let ds = tag(0x0c) else { return nil }
        lineHeight = lh
        descent = ds
        // "03 08 <n> 04 08 <?> 01 04": the count, then the characters.
        var start = -1
        var p = 0
        while p + 14 <= d.count {
            if d[p] == 3, d[p + 1] == 8, d[p + 6] == 4, d[p + 7] == 8, d[p + 12] == 1, d[p + 13] == 4 {
                start = p
                break
            }
            p += 1
        }
        guard start >= 0 else { return nil }
        let n = i32(start + 2)
        guard n > 0, n < 4096 else { return nil }
        p = start + 12
        var chars: [UInt16] = []
        for _ in 0..<n {
            guard p + 4 <= d.count, d[p] == 1, d[p + 1] == 4 else { return nil }
            chars.append(UInt16(d[p + 2]) | UInt16(d[p + 3]) << 8)
            p += 4
        }
        for c in chars {
            guard p + 30 <= d.count, d[p] == 2, d[p + 1] == 0x38 else { return nil }
            glyphs.append((c, i32(p + 2), i32(p + 6), i32(p + 10), i32(p + 14), i32(p + 18), i32(p + 22)))
            p += 30
        }
    }
}

/// Outlines from letter pictures, and the TrueType file round them.
enum FontWriter {
    static let K = 4                 // smoothing: picture pixels per traced pixel
    static let ink = 140.0           // alpha that counts as ink (the game's stroke weight)
    static let tolerance = 0.7       // simplification, in traced pixels

    typealias Contour = [(Int, Int)]

    static func build(glyphs table: GlyphTable, alpha: (Int, Int) -> UInt8, emPx: Int,
                      ps: String, style: String, bold: Bool) -> Data {
        let upm = 1000
        let u = Double(upm) / Double(emPx)
        let ascentPx = table.lineHeight - table.descent
        let sorted = table.glyphs.sorted { $0.char < $1.char }
        var outlines: [[Contour]] = [[]]        // glyph 0: .notdef, empty
        var advances: [Int] = [upm / 2]
        for g in sorted {
            advances.append(Int((Double(g.a + g.b) * u).rounded()))
            let contours = trace(x1: g.x1, y1: g.y1, x2: g.x2, y2: g.y2, alpha: alpha).map { c in
                c.map { (x, y) -> (Int, Int) in
                    (Int(((Double(g.a) + Double(x) / Double(K)) * u).rounded()),
                     Int(((Double(ascentPx) - Double(y) / Double(K)) * u).rounded()))
                }
            }
            outlines.append(contours)
        }
        return write(outlines: outlines, advances: advances, chars: sorted.map { $0.char },
                     ascent: Int(Double(ascentPx) * u), descent: Int(Double(table.descent) * u),
                     upm: upm, ps: ps, style: style, bold: bold)
    }

    /// A letter's ink as closed outlines, in traced pixels (y down).
    static func trace(x1: Int, y1: Int, x2: Int, y2: Int, alpha: (Int, Int) -> UInt8) -> [Contour] {
        let cw = x2 - x1, ch = y2 - y1
        guard cw > 0, ch > 0 else { return [] }
        let W = cw * K, H = ch * K
        var grid = [Bool](repeating: false, count: W * H)
        for j in 0..<H {
            let sy = (Double(j) + 0.5) / Double(K) - 0.5
            let y0 = Int(sy.rounded(.down)), fy = sy - Double(y0)
            for i in 0..<W {
                let sx = (Double(i) + 0.5) / Double(K) - 0.5
                let x0 = Int(sx.rounded(.down)), fx = sx - Double(x0)
                func a(_ x: Int, _ y: Int) -> Double {
                    x < 0 || y < 0 || x >= cw || y >= ch ? 0 : Double(alpha(x1 + x, y1 + y))
                }
                let v = (a(x0, y0) * (1 - fx) + a(x0 + 1, y0) * fx) * (1 - fy)
                    + (a(x0, y0 + 1) * (1 - fx) + a(x0 + 1, y0 + 1) * fx) * fy
                grid[j * W + i] = v >= ink
            }
        }
        func on(_ i: Int, _ j: Int) -> Bool { i >= 0 && j >= 0 && i < W && j < H && grid[j * W + i] }
        // The ink's boundary as directed edges, the ink on their right
        // (clockwise round each ink pixel, y down); shared sides drop out.
        var next: [Int: [Int]] = [:]   // vertex -> vertices an edge goes to
        func v(_ x: Int, _ y: Int) -> Int { y * (W + 1) + x }
        for j in 0..<H {
            for i in 0..<W where grid[j * W + i] {
                if !on(i, j - 1) { next[v(i, j), default: []].append(v(i + 1, j)) }
                if !on(i + 1, j) { next[v(i + 1, j), default: []].append(v(i + 1, j + 1)) }
                if !on(i, j + 1) { next[v(i + 1, j + 1), default: []].append(v(i, j + 1)) }
                if !on(i - 1, j) { next[v(i, j + 1), default: []].append(v(i, j)) }
            }
        }
        var contours: [Contour] = []
        while let (start, ends) = next.first(where: { !$0.value.isEmpty }) {
            var loop: Contour = []
            var from = start, to = ends[0]
            next[start]!.removeFirst()
            var guardSteps = W * H * 4 + 8
            while guardSteps > 0 {
                guardSteps -= 1
                loop.append((from % (W + 1), from / (W + 1)))
                if to == start { break }
                // At a corner two edges leave: turn right, keeping touching
                // blobs apart.
                guard var outs = next[to], !outs.isEmpty else { break }
                var pick = 0
                if outs.count > 1 {
                    let dx = to % (W + 1) - from % (W + 1), dy = to / (W + 1) - from / (W + 1)
                    let right = (-dy, dx) // y down: a right turn
                    pick = outs.firstIndex { ($0 % (W + 1) - to % (W + 1), $0 / (W + 1) - to / (W + 1)) == right } ?? 0
                }
                let n = outs.remove(at: pick)
                next[to] = outs
                from = to
                to = n
            }
            let simple = simplify(dropStraight(loop))
            if simple.count >= 3 { contours.append(simple) }
        }
        return contours
    }

    static func dropStraight(_ c: Contour) -> Contour {
        guard c.count > 3 else { return c }
        var out: Contour = []
        for k in 0..<c.count {
            let p = c[(k + c.count - 1) % c.count], q = c[k], r = c[(k + 1) % c.count]
            if (q.0 - p.0) * (r.1 - q.1) != (q.1 - p.1) * (r.0 - q.0) { out.append(q) }
        }
        return out
    }

    /// Douglas-Peucker on a closed outline: split at the point farthest from
    /// the first, simplify both halves.
    static func simplify(_ c: Contour) -> Contour {
        guard c.count > 4 else { return c }
        var far = 0, best = -1
        for (k, p) in c.enumerated() {
            let d = (p.0 - c[0].0) * (p.0 - c[0].0) + (p.1 - c[0].1) * (p.1 - c[0].1)
            if d > best { best = d; far = k }
        }
        let a = dp(Array(c[0...far])), b = dp(Array(c[far...]) + [c[0]])
        return Array(a.dropLast()) + Array(b.dropLast())
    }

    static func dp(_ pts: Contour) -> Contour {
        guard pts.count > 2 else { return pts }
        let (ax, ay) = (Double(pts[0].0), Double(pts[0].1))
        let (bx, by) = (Double(pts.last!.0), Double(pts.last!.1))
        let len = max(1e-9, ((bx - ax) * (bx - ax) + (by - ay) * (by - ay)).squareRoot())
        var far = 0, best = 0.0
        for k in 1..<(pts.count - 1) {
            let d = abs((bx - ax) * (ay - Double(pts[k].1)) - (ax - Double(pts[k].0)) * (by - ay)) / len
            if d > best { best = d; far = k }
        }
        if best <= tolerance { return [pts[0], pts.last!] }
        return Array(dp(Array(pts[0...far])).dropLast()) + dp(Array(pts[far...]))
    }

    // MARK: - The TrueType file

    static func write(outlines: [[Contour]], advances: [Int], chars: [UInt16], ascent: Int, descent: Int,
                      upm: Int, ps: String, style: String, bold: Bool) -> Data {
        let n = outlines.count
        var glyf = Data(), loca = [Int](), lsbs = [Int]()
        var bbox = (Int.max, Int.max, Int.min, Int.min)
        var maxPoints = 0, maxContours = 0
        for outline in outlines {
            loca.append(glyf.count)
            // Outer outlines clockwise with y up: the traced ones (clockwise
            // with y down) reversed.
            let cs = outline.map { Array($0.reversed()) }
            let pts = cs.flatMap { $0 }
            guard !pts.isEmpty else { lsbs.append(0); continue }
            let xs = pts.map { $0.0 }, ys = pts.map { $0.1 }
            let b = (xs.min()!, ys.min()!, xs.max()!, ys.max()!)
            bbox = (min(bbox.0, b.0), min(bbox.1, b.1), max(bbox.2, b.2), max(bbox.3, b.3))
            lsbs.append(b.0)
            maxPoints = max(maxPoints, pts.count)
            maxContours = max(maxContours, cs.count)
            var g = Data()
            g.i16(cs.count); g.i16(b.0); g.i16(b.1); g.i16(b.2); g.i16(b.3)
            var end = -1
            for c in cs { end += c.count; g.u16(end) }
            g.u16(0) // no instructions
            for _ in pts { g.append(0x01) } // on the curve, coordinates as int16
            var last = 0
            for p in pts { g.i16(p.0 - last); last = p.0 }
            last = 0
            for p in pts { g.i16(p.1 - last); last = p.1 }
            while g.count % 4 != 0 { g.append(0) }
            glyf.append(g)
        }
        loca.append(glyf.count)
        if bbox.0 == Int.max { bbox = (0, -descent, upm, ascent) }

        var head = Data()
        head.u32(0x0001_0000); head.u32(0x0001_0000); head.u32(0); head.u32(0x5F0F_3CF5)
        head.u16(0x000B); head.u16(upm); head.u64(0); head.u64(0)
        head.i16(bbox.0); head.i16(bbox.1); head.i16(bbox.2); head.i16(bbox.3)
        head.u16((bold ? 1 : 0) | 2); head.u16(8); head.i16(2); head.i16(1); head.i16(0)

        var hhea = Data()
        hhea.u32(0x0001_0000); hhea.i16(ascent); hhea.i16(-descent); hhea.i16(0)
        hhea.u16(advances.max() ?? upm); hhea.i16(lsbs.min() ?? 0); hhea.i16(0); hhea.i16(bbox.2)
        hhea.i16(1); hhea.i16(0); hhea.i16(0); for _ in 0..<4 { hhea.i16(0) }
        hhea.i16(0); hhea.u16(n)

        var maxp = Data()
        maxp.u32(0x0001_0000); maxp.u16(n); maxp.u16(maxPoints); maxp.u16(maxContours)
        maxp.u16(0); maxp.u16(0); maxp.u16(2); for _ in 0..<9 { maxp.u16(0) }

        var hmtx = Data()
        for k in 0..<n { hmtx.u16(advances[k]); hmtx.i16(lsbs[k]) }

        var locaData = Data()
        for o in loca { locaData.u32(o) }

        // cmap: format 4, a segment per character, then the closing one.
        var seg = chars.enumerated().map { (code: Int($0.element), gid: $0.offset + 1) }
        seg.append((0xFFFF, 0))
        var f4 = Data()
        let segX2 = seg.count * 2
        var sr = 2, es = 0
        while sr * 2 <= segX2 { sr *= 2; es += 1 }
        f4.u16(4); f4.u16(16 + seg.count * 8); f4.u16(0); f4.u16(segX2); f4.u16(sr); f4.u16(es); f4.u16(segX2 - sr)
        for s in seg { f4.u16(s.code) }
        f4.u16(0)
        for s in seg { f4.u16(s.code) }
        for s in seg { f4.u16(s.code == 0xFFFF ? 1 : (s.gid - s.code) & 0xFFFF) }
        for _ in seg { f4.u16(0) }
        var cmap = Data()
        cmap.u16(0); cmap.u16(2)
        cmap.u16(0); cmap.u16(3); cmap.u32(20)
        cmap.u16(3); cmap.u16(1); cmap.u32(20)
        cmap.append(f4)

        let family = "HeroesV"
        let names: [(Int, String)] = [(1, family), (2, style), (3, ps), (4, family + " " + style), (6, ps)]
        var strings = Data()
        var records = Data()
        for (id, s) in names {
            let utf16 = s.data(using: .utf16BigEndian)!
            records.u16(3); records.u16(1); records.u16(0x409); records.u16(id)
            records.u16(utf16.count); records.u16(strings.count)
            strings.append(utf16)
        }
        var name = Data()
        name.u16(0); name.u16(names.count); name.u16(6 + names.count * 12)
        name.append(records); name.append(strings)

        var os2 = Data()
        os2.u16(4); os2.i16(advances.reduce(0, +) / max(1, n)); os2.u16(bold ? 700 : 400); os2.u16(5); os2.u16(0)
        for v in [650, 600, 0, 75, 650, 600, 0, 350, 50, 260] { os2.i16(v) }
        os2.i16(0) // family class
        for _ in 0..<10 { os2.append(0) }
        os2.u32(1 | 1 << 9); os2.u32(0); os2.u32(0); os2.u32(0) // Basic Latin, Cyrillic
        os2.append(contentsOf: Array("HMRE".utf8))
        os2.u16(bold ? 0x21 : 0x01)
        os2.u16(Int(chars.first ?? 32)); os2.u16(Int(chars.last ?? 0xFFFF))
        os2.i16(ascent); os2.i16(-descent); os2.i16(0); os2.u16(ascent); os2.u16(descent)
        os2.u32(0x05); os2.u32(0) // Latin 1, Cyrillic
        os2.i16(0); os2.i16(0); os2.u16(0); os2.u16(32); os2.u16(1)

        var post = Data()
        post.u32(0x0003_0000); post.u32(UInt32(bitPattern: Int32(-12 << 16))); post.i16(-100); post.i16(50)
        post.u32(0); post.u32(0); post.u32(0); post.u32(0); post.u32(0)

        let tables: [(String, Data)] = [("OS/2", os2), ("cmap", cmap), ("glyf", glyf), ("head", head), ("hhea", hhea),
                                        ("hmtx", hmtx), ("loca", locaData), ("maxp", maxp), ("name", name),
                                        ("post", post)]
        var out = Data()
        var sr2 = 1, es2 = 0
        while sr2 * 2 <= tables.count { sr2 *= 2; es2 += 1 }
        out.u32(0x0001_0000); out.u16(tables.count); out.u16(sr2 * 16); out.u16(es2); out.u16(tables.count * 16 - sr2 * 16)
        var offset = 12 + tables.count * 16
        var body = Data()
        var headOffset = 0
        for (tag, data) in tables {
            var padded = data
            while padded.count % 4 != 0 { padded.append(0) }
            out.append(contentsOf: Array(tag.utf8))
            out.u32(Int(checksum(padded)))
            out.u32(offset); out.u32(data.count)
            if tag == "head" { headOffset = offset }
            body.append(padded)
            offset += padded.count
        }
        out.append(body)
        // head.checkSumAdjustment: the whole file sums to 0xB1B0AFBA.
        let adjust = UInt32(0xB1B0_AFBA) &- checksum(out)
        out.replaceSubrange((headOffset + 8)..<(headOffset + 12),
                            with: [UInt8(adjust >> 24), UInt8(adjust >> 16 & 0xFF), UInt8(adjust >> 8 & 0xFF), UInt8(adjust & 0xFF)])
        return out
    }

    static func checksum(_ d: Data) -> UInt32 {
        var sum: UInt32 = 0
        let b = [UInt8](d)
        var k = 0
        while k < b.count {
            var w: UInt32 = 0
            for j in 0..<4 { w = w << 8 | UInt32(k + j < b.count ? b[k + j] : 0) }
            sum = sum &+ w
            k += 4
        }
        return sum
    }
}

private extension Data {
    mutating func u16(_ v: Int) { append(UInt8(v >> 8 & 0xFF)); append(UInt8(v & 0xFF)) }
    mutating func i16(_ v: Int) { u16(Int(UInt16(bitPattern: Int16(clamping: v)))) }
    mutating func u32(_ v: Int) { u32(UInt32(truncatingIfNeeded: v)) }
    mutating func u32(_ v: UInt32) {
        append(UInt8(v >> 24)); append(UInt8(v >> 16 & 0xFF)); append(UInt8(v >> 8 & 0xFF)); append(UInt8(v & 0xFF))
    }
    mutating func u64(_ v: UInt64) { u32(UInt32(v >> 32)); u32(UInt32(v & 0xFFFF_FFFF)) }
}
