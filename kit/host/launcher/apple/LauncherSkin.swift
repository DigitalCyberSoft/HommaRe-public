// LauncherSkin.swift - the game's own look for the launcher: buttons, scrolls,
// parchment and dialog frames read from the player's install (data/*.pak are
// zip archives of DDS textures), never shipped with the app. Without a game
// on this device the launcher keeps its flat look (LauncherView.swift).
//
// The first load decodes the textures and bakes them as PNG files into
// Application Support/<app>/LauncherSkin/<stamp>; later launches read those
// while the paks stay the same (the stamp is their names, sizes and dates).
import Compression
import CoreGraphics
import CryptoKit
import Foundation
import ImageIO
import SwiftUI

#if canImport(AppKit)
import AppKit
#else
import UIKit
#endif

// MARK: - Textures

/// Every texture the launcher draws, with its path inside the paks and the
/// part of it that holds the picture (the game pads textures to powers of two).
enum SkinTexture: String, CaseIterable, Sendable {
    case logo, sky, scroll, parchment, page, darkBox, dialog, messageIcon
    case button, buttonHighlight, buttonPushed, buttonDisabled
    case smallButton, smallButtonHighlight, smallButtonPushed, smallButtonDisabled
    case okButton, okButtonHighlight, okButtonPushed, okButtonDisabled
    case rowSelected, rowHover, track, checkOff, checkOn

    var path: String {
        switch self {
        case .logo: return "UI/MainMenu2/Logo.(Texture).dds"
        case .sky: return "Arenas/Interface/MainMenu/pPlane1-2.(Texture).dds"
        case .scroll: return "UI/MainMenu2/SelectCampaign/MenuPanel.(Texture).dds"
        case .parchment: return "UI/MainMenu2/SetupPanel/SetupPanel.(Texture).dds"
        case .page: return "UI/GameOptions/MainBackground.(Texture).dds"
        case .darkBox: return "UI/MainMenu2/SetupPanel/DifficultyList.(Texture).dds"
        case .dialog: return "Textures/Interface/CommonBoxes/Box0p/back_texture.dds"
        case .messageIcon: return "UI/MessageBox/Message.dds"
        case .button: return "UI/Common/Button_136x49/Active.(Texture).dds"
        case .buttonHighlight: return "UI/Common/Button_136x49/Highlight.(Texture).dds"
        case .buttonPushed: return "UI/Common/Button_136x49/Pushed.(Texture).dds"
        case .buttonDisabled: return "UI/Common/Button_136x49/Disabled.(Texture).dds"
        case .smallButton: return "UI/Common/DlgButtons/Button_122_34_Normal.(Texture).dds"
        case .smallButtonHighlight: return "UI/Common/DlgButtons/Button_122_34_Highlight.(Texture).dds"
        case .smallButtonPushed: return "UI/Common/DlgButtons/Button_122_34_Click.(Texture).dds"
        case .smallButtonDisabled: return "UI/Common/DlgButtons/Button_122_34_Disabled.(Texture).dds"
        case .okButton: return "Textures/Interface/CommonBoxes/button_ok/Normal.dds"
        case .okButtonHighlight: return "Textures/Interface/CommonBoxes/button_ok/Highlight.dds"
        case .okButtonPushed: return "Textures/Interface/CommonBoxes/button_ok/Pushed.dds"
        case .okButtonDisabled: return "Textures/Interface/CommonBoxes/button_ok/Disabled.dds"
        case .rowSelected: return "UI/MainMenu2/ChangeProfile/ProfileListSelection.(Texture).dds"
        case .rowHover: return "UI/MainMenu2/ChangeProfile/ProfileListPreselection.(Texture).dds"
        case .track: return "UI/GameOptions/Templates/ScrollBarPart/ScrollBarBg.(Texture).dds"
        case .checkOff: return "UI/GameOptions/Templates/CheckBox/Button_0_Normal.(Texture).dds"
        case .checkOn: return "UI/GameOptions/Templates/CheckBox/Button_1_Normal.(Texture).dds"
        }
    }

    /// The picture's x, y, width and height in the texture; nil for the logo,
    /// which differs between the games and is cut to its opaque pixels.
    var rect: (Int, Int, Int, Int)? {
        switch self {
        case .logo: return nil
        case .sky: return (0, 0, 1024, 800)
        case .scroll: return (0, 0, 328, 506)
        case .parchment: return (0, 1, 342, 112)
        case .page: return (0, 0, 711, 717)
        case .darkBox: return (0, 0, 122, 81)
        case .dialog: return (0, 2, 525, 328)
        case .messageIcon: return (4, 1, 124, 126)
        case .button, .buttonPushed, .buttonDisabled: return (0, 0, 136, 48)
        case .buttonHighlight: return (0, 0, 136, 49)
        case .smallButton, .smallButtonHighlight, .smallButtonPushed, .smallButtonDisabled: return (0, 0, 122, 34)
        case .okButton, .okButtonPushed, .okButtonDisabled: return (0, 0, 97, 48)
        case .okButtonHighlight: return (0, 0, 97, 49)
        case .rowSelected: return (0, 0, 382, 21)
        case .rowHover: return (0, 0, 382, 15)
        case .track: return (0, 0, 201, 24)
        case .checkOff: return (0, 0, 25, 25)
        case .checkOn: return (0, 0, 27, 25)
        }
    }
}

extension SkinTexture {
    /// Pictures the launcher can do without (a plain backdrop, no icon).
    var optional: Bool {
        self == .sky || self == .messageIcon
    }

    /// Light the game adds onto what is below (CONVERT_TRANSPARENT_ADD in the
    /// texture's .xdb): colour only, its alpha is empty. Decoded opaque, its
    /// violet-blue turned into warm gold light, and drawn with the
    /// plus-lighter blend mode.
    var additive: Bool {
        switch self {
        case .buttonHighlight, .smallButtonHighlight, .okButtonHighlight, .rowSelected, .rowHover: return true
        default: return false
        }
    }
}

// MARK: - Skin

final class Skin: @unchecked Sendable {
    let images: [SkinTexture: CGImage]
    /// The install the textures came from.
    let root: URL

    init(images: [SkinTexture: CGImage], root: URL) {
        self.images = images
        self.root = root
    }

    subscript(_ t: SkinTexture) -> CGImage? {
        images[t]
    }

    func image(_ t: SkinTexture) -> Image {
        images[t].map { Image(decorative: $0, scale: 1) } ?? Image(decorative: Self.clear, scale: 1)
    }

    /// The texture drawn as a nine-slice: corners kept, edges and middle stretched.
    func sliced(_ t: SkinTexture, _ top: CGFloat, _ leading: CGFloat, _ bottom: CGFloat, _ trailing: CGFloat) -> some View {
        image(t).resizable(capInsets: EdgeInsets(top: top, leading: leading, bottom: bottom, trailing: trailing),
                           resizingMode: .stretch)
    }

    private static let clear: CGImage = {
        let ctx = CGContext(data: nil, width: 1, height: 1, bitsPerComponent: 8, bytesPerRow: 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        return ctx.makeImage()!
    }()

    /// The paks of a game install, in the order the game reads them (later
    /// ones override earlier ones: data.pak, then the patches' p*-data.pak).
    static func paks(in root: URL) -> [URL] {
        guard let data = Detector.entry(root, "data"),
              let names = try? FileManager.default.contentsOfDirectory(atPath: data.path) else { return [] }
        let paks = names.filter { n in
            let l = n.lowercased()
            return l.hasSuffix(".pak") && (l == "data.pak" || l.hasSuffix("-data.pak"))
        }
        return paks.sorted { a, b in
            let la = a.lowercased(), lb = b.lowercased()
            if la == "data.pak" || lb == "data.pak" { return la == "data.pak" && lb != "data.pak" }
            return la < lb
        }.map { data.appendingPathComponent($0) }
    }

    /// The textures from `root`, from the baked cache when the paks have not
    /// changed since. nil when the install lacks any of them.
    static func load(root: URL) -> Skin? {
        let paks = paks(in: root)
        guard !paks.isEmpty else { return nil }
        let cacheDir = cacheBase()?.appendingPathComponent(stamp(paks), isDirectory: true)
        if let dir = cacheDir, let images = readCache(dir) {
            return Skin(images: images, root: root)
        }
        let archives = paks.compactMap { PakArchive(url: $0) }
        var images: [SkinTexture: CGImage] = [:]
        for t in SkinTexture.allCases {
            // The last pak that has the texture wins.
            guard let data = archives.reversed().lazy.compactMap({ $0.read(t.path) }).first,
                  let image = DDSImage.decode(data, crop: t.rect, light: t.additive ? DDSImage.warmLight : nil) else {
                if t.optional { continue }
                FileHandle.standardError.write(Data("[launcher] skin: no \(t.path) in \(root.path)\n".utf8))
                return nil
            }
            images[t] = image
        }
        if let dir = cacheDir { writeCache(images, to: dir) }
        return Skin(images: images, root: root)
    }

    private static let cacheVersion = "5"

    private static func cacheBase() -> URL? {
        guard let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else { return nil }
        let app = Bundle.main.object(forInfoDictionaryKey: "CFBundleName") as? String ?? "Recomp"
        return support.appendingPathComponent(app).appendingPathComponent("LauncherSkin", isDirectory: true)
    }

    private static func stamp(_ paks: [URL]) -> String {
        var h = SHA256()
        h.update(data: Data(cacheVersion.utf8))
        for p in paks {
            let a = try? FileManager.default.attributesOfItem(atPath: p.path)
            let size = (a?[.size] as? NSNumber)?.int64Value ?? 0
            let date = (a?[.modificationDate] as? Date)?.timeIntervalSince1970 ?? 0
            h.update(data: Data("\(p.path)|\(size)|\(date)\n".utf8))
        }
        return h.finalize().prefix(8).map { String(format: "%02x", $0) }.joined()
    }

    private static func readCache(_ dir: URL) -> [SkinTexture: CGImage]? {
        var images: [SkinTexture: CGImage] = [:]
        // The bake lists what it holds: optional pictures may be absent.
        guard let list = try? String(contentsOf: dir.appendingPathComponent("textures.txt"), encoding: .utf8) else { return nil }
        let have = Set(list.split(separator: "\n").map(String.init))
        for t in SkinTexture.allCases where have.contains(t.rawValue) || !t.optional {
            let url = dir.appendingPathComponent(t.rawValue + ".png")
            guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
                  let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }
            images[t] = image
        }
        return images
    }

    private static func writeCache(_ images: [SkinTexture: CGImage], to dir: URL) {
        let fm = FileManager.default
        // One stamp at a time: older bakes go.
        if let base = cacheBase() { try? fm.removeItem(at: base) }
        guard (try? fm.createDirectory(at: dir, withIntermediateDirectories: true)) != nil else { return }
        for (t, image) in images {
            let url = dir.appendingPathComponent(t.rawValue + ".png")
            guard let dest = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else { continue }
            CGImageDestinationAddImage(dest, image, nil)
            CGImageDestinationFinalize(dest)
        }
        // Written last: a bake without it is unfinished and is redone.
        try? images.keys.map(\.rawValue).sorted().joined(separator: "\n")
            .write(to: dir.appendingPathComponent("textures.txt"), atomically: true, encoding: .utf8)
        #if os(iOS)
        var values = URLResourceValues()
        values.isExcludedFromBackup = true
        var d = dir
        try? d.setResourceValues(values)
        #endif
    }
}

// MARK: - Pak archives

/// Reads single entries of a game pak: a plain zip (stored or deflated, no
/// zip64), indexed by its central directory with names compared without case.
struct PakArchive {
    private struct Entry {
        var method: UInt16
        var compressedSize: Int
        var size: Int
        var localHeader: UInt64
    }

    let url: URL
    private var entries: [String: Entry] = [:]

    init?(url: URL) {
        self.url = url
        guard let h = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? h.close() }
        guard let end = try? h.seekToEnd(), end >= 22 else { return nil }
        // The end-of-central-directory record: within the last 64 KiB + 22.
        let tailLen = min(end, 65_557)
        try? h.seek(toOffset: end - tailLen)
        guard let tail = try? h.read(upToCount: Int(tailLen)), tail.count == Int(tailLen) else { return nil }
        var eocd = -1
        var i = tail.count - 22
        while i >= 0 {
            if tail.u32(i) == 0x0605_4b50 { eocd = i; break }
            i -= 1
        }
        guard eocd >= 0 else { return nil }
        let cdSize = Int(tail.u32(eocd + 12)), cdOffset = UInt64(tail.u32(eocd + 16))
        guard cdOffset != 0xffff_ffff, cdOffset + UInt64(cdSize) <= end else { return nil }
        try? h.seek(toOffset: cdOffset)
        guard let cd = try? h.read(upToCount: cdSize), cd.count == cdSize else { return nil }
        var p = 0
        while p + 46 <= cd.count, cd.u32(p) == 0x0201_4b50 {
            let nameLen = Int(cd.u16(p + 28)), extraLen = Int(cd.u16(p + 30)), commentLen = Int(cd.u16(p + 32))
            guard p + 46 + nameLen <= cd.count else { break }
            let nameData = cd.subdata(in: (cd.startIndex + p + 46)..<(cd.startIndex + p + 46 + nameLen))
            let name = String(decoding: nameData, as: UTF8.self).lowercased()
            entries[name] = Entry(method: cd.u16(p + 10), compressedSize: Int(cd.u32(p + 20)), size: Int(cd.u32(p + 24)),
                                  localHeader: UInt64(cd.u32(p + 42)))
            p += 46 + nameLen + extraLen + commentLen
        }
        if entries.isEmpty { return nil }
    }

    func read(_ path: String) -> Data? {
        guard let e = entries[path.lowercased()], let h = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? h.close() }
        try? h.seek(toOffset: e.localHeader)
        guard let local = try? h.read(upToCount: 30), local.count == 30, local.u32(0) == 0x0403_4b50 else { return nil }
        try? h.seek(toOffset: e.localHeader + 30 + UInt64(local.u16(26)) + UInt64(local.u16(28)))
        guard let raw = try? h.read(upToCount: e.compressedSize), raw.count == e.compressedSize else { return nil }
        switch e.method {
        case 0:
            return raw
        case 8:
            // Compression's ZLIB is raw deflate, the zip method 8 stream.
            var out = Data(count: e.size)
            let n = out.withUnsafeMutableBytes { dst in
                raw.withUnsafeBytes { src in
                    compression_decode_buffer(dst.bindMemory(to: UInt8.self).baseAddress!, e.size,
                                              src.bindMemory(to: UInt8.self).baseAddress!, raw.count, nil, COMPRESSION_ZLIB)
                }
            }
            return n == e.size ? out : nil
        default:
            return nil
        }
    }
}

private extension Data {
    func u16(_ at: Int) -> UInt16 {
        UInt16(self[startIndex + at]) | UInt16(self[startIndex + at + 1]) << 8
    }

    func u32(_ at: Int) -> UInt32 {
        UInt32(u16(at)) | UInt32(u16(at + 2)) << 16
    }
}

// MARK: - DDS

/// DirectDraw Surface textures as the game ships them: DXT1, DXT3 and DXT5
/// blocks, or uncompressed pixels described by channel masks.
enum DDSImage {
    /// The texture cut to `crop` (x, y, width, height), or to its opaque
    /// pixels without one.
    /// Gold (#F6C94C) as fractions: the colour additive textures are lit with.
    static let warmLight: (Double, Double, Double) = (1, 0.82, 0.31)

    /// With `light`, the texture is additive: each pixel becomes `light` with
    /// the pixel's brightness as its alpha, so premultiplied it is `light`
    /// scaled by brightness: plus-lighter adds exactly that, and where the
    /// blend mode is not honoured the light is laid over, never a black box.
    static func decode(_ d: Data, crop: (Int, Int, Int, Int)?, light: (Double, Double, Double)? = nil) -> CGImage? {
        guard d.count >= 128, d.u32(0) == 0x2053_4444 /* "DDS " */ else { return nil }
        let height = Int(d.u32(12)), width = Int(d.u32(16))
        let pfFlags = d.u32(80), fourCC = d.u32(84), bits = Int(d.u32(88))
        guard width > 0, height > 0, width <= 8192, height <= 8192 else { return nil }
        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        let ok: Bool = d.withUnsafeBytes { raw in
            let src = raw.bindMemory(to: UInt8.self)
            let body = UnsafeBufferPointer(rebasing: src[128...])
            if pfFlags & 0x4 != 0 {
                switch fourCC {
                case 0x3154_5844: return decodeBlocks(body, width, height, kind: 1, into: &rgba)
                case 0x3354_5844: return decodeBlocks(body, width, height, kind: 3, into: &rgba)
                case 0x3554_5844: return decodeBlocks(body, width, height, kind: 5, into: &rgba)
                default: return false
                }
            }
            let masks = (d.u32(92), d.u32(96), d.u32(100), pfFlags & 0x1 != 0 ? d.u32(104) : 0)
            return decodeMasked(body, width, height, bits: bits, masks: masks, into: &rgba)
        }
        guard ok else { return nil }
        if let light {
            for i in stride(from: 0, to: rgba.count, by: 4) {
                let v = max(rgba[i], rgba[i + 1], rgba[i + 2])
                rgba[i] = UInt8(255 * light.0)
                rgba[i + 1] = UInt8(255 * light.1)
                rgba[i + 2] = UInt8(255 * light.2)
                rgba[i + 3] = v
            }
        }
        // An additive texture keeps its whole size (its dark parts are clear now).
        var (cx, cy, cw, ch) = crop ?? (light != nil ? (0, 0, width, height) : opaqueBounds(rgba, width, height))
        cx = min(cx, width - 1); cy = min(cy, height - 1)
        cw = min(cw, width - cx); ch = min(ch, height - cy)
        var out = [UInt8](repeating: 0, count: cw * ch * 4)
        for y in 0..<ch {
            for x in 0..<cw {
                let s = ((y + cy) * width + x + cx) * 4, o = (y * cw + x) * 4
                // Premultiplied, the form Core Graphics draws fastest.
                let a = UInt16(rgba[s + 3])
                out[o] = UInt8((UInt16(rgba[s]) * a + 127) / 255)
                out[o + 1] = UInt8((UInt16(rgba[s + 1]) * a + 127) / 255)
                out[o + 2] = UInt8((UInt16(rgba[s + 2]) * a + 127) / 255)
                out[o + 3] = UInt8(a)
            }
        }
        guard let provider = CGDataProvider(data: Data(out) as CFData) else { return nil }
        return CGImage(width: cw, height: ch, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: cw * 4,
                       space: CGColorSpace(name: CGColorSpace.sRGB)!,
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
    }

    private static func opaqueBounds(_ px: [UInt8], _ w: Int, _ h: Int) -> (Int, Int, Int, Int) {
        var minX = w, minY = h, maxX = -1, maxY = -1
        for y in 0..<h {
            for x in 0..<w where px[(y * w + x) * 4 + 3] != 0 {
                minX = min(minX, x); maxX = max(maxX, x)
                minY = min(minY, y); maxY = max(maxY, y)
            }
        }
        return maxX < 0 ? (0, 0, w, h) : (minX, minY, maxX - minX + 1, maxY - minY + 1)
    }

    private static func rgb565(_ c: UInt16) -> (Int, Int, Int) {
        let r = Int(c >> 11 & 31), g = Int(c >> 5 & 63), b = Int(c & 31)
        return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    }

    private static func decodeBlocks(_ s: UnsafeBufferPointer<UInt8>, _ w: Int, _ h: Int, kind: Int,
                                     into px: inout [UInt8]) -> Bool {
        let blockBytes = kind == 1 ? 8 : 16
        let bw = (w + 3) / 4, bh = (h + 3) / 4
        guard s.count >= bw * bh * blockBytes else { return false }
        func u16(_ i: Int) -> UInt16 { UInt16(s[i]) | UInt16(s[i + 1]) << 8 }
        var off = 0
        for by in 0..<bh {
            for bx in 0..<bw {
                var alpha = [UInt8](repeating: 255, count: 16)
                var c = off
                if kind == 3 {
                    for i in 0..<16 {
                        let nib = (s[off + i / 2] >> (UInt8(i % 2) * 4)) & 0xf
                        alpha[i] = nib << 4 | nib
                    }
                    c += 8
                } else if kind == 5 {
                    let a0 = Int(s[off]), a1 = Int(s[off + 1])
                    var table = [a0, a1, 0, 0, 0, 0, 0, 0]
                    if a0 > a1 {
                        for i in 1...6 { table[i + 1] = ((7 - i) * a0 + i * a1) / 7 }
                    } else {
                        for i in 1...4 { table[i + 1] = ((5 - i) * a0 + i * a1) / 5 }
                        table[6] = 0; table[7] = 255
                    }
                    var bitsA: UInt64 = 0
                    for i in 0..<6 { bitsA |= UInt64(s[off + 2 + i]) << (8 * UInt64(i)) }
                    for i in 0..<16 { alpha[i] = UInt8(table[Int(bitsA >> (3 * UInt64(i)) & 7)]) }
                    c += 8
                }
                let c0 = u16(c), c1 = u16(c + 2)
                let (r0, g0, b0) = rgb565(c0), (r1, g1, b1) = rgb565(c1)
                var pal = [(r0, g0, b0, 255), (r1, g1, b1, 255), (0, 0, 0, 255), (0, 0, 0, 255)]
                if c0 > c1 || kind != 1 {
                    pal[2] = ((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255)
                    pal[3] = ((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255)
                } else {
                    pal[2] = ((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255)
                    pal[3] = (0, 0, 0, 0)
                }
                let idx = UInt32(s[c + 4]) | UInt32(s[c + 5]) << 8 | UInt32(s[c + 6]) << 16 | UInt32(s[c + 7]) << 24
                for i in 0..<16 {
                    let x = bx * 4 + i % 4, y = by * 4 + i / 4
                    guard x < w, y < h else { continue }
                    let p = pal[Int(idx >> (2 * UInt32(i)) & 3)]
                    let o = (y * w + x) * 4
                    px[o] = UInt8(p.0); px[o + 1] = UInt8(p.1); px[o + 2] = UInt8(p.2)
                    px[o + 3] = kind == 1 ? UInt8(p.3) : alpha[i]
                }
                off += blockBytes
            }
        }
        return true
    }

    private static func decodeMasked(_ s: UnsafeBufferPointer<UInt8>, _ w: Int, _ h: Int, bits: Int,
                                     masks: (UInt32, UInt32, UInt32, UInt32), into px: inout [UInt8]) -> Bool {
        guard bits == 16 || bits == 24 || bits == 32 else { return false }
        let bpp = bits / 8
        guard s.count >= w * h * bpp else { return false }
        func channel(_ v: UInt32, _ mask: UInt32) -> UInt8 {
            guard mask != 0 else { return 255 }
            let shift = UInt32(mask.trailingZeroBitCount)
            let maxV = mask >> shift
            return UInt8((UInt64((v & mask) >> shift) * 255 + UInt64(maxV) / 2) / UInt64(maxV))
        }
        for i in 0..<(w * h) {
            var v: UInt32 = 0
            for b in 0..<bpp { v |= UInt32(s[i * bpp + b]) << (8 * UInt32(b)) }
            px[i * 4] = channel(v, masks.0)
            px[i * 4 + 1] = channel(v, masks.1)
            px[i * 4 + 2] = channel(v, masks.2)
            px[i * 4 + 3] = channel(v, masks.3)
        }
        return true
    }
}
