#!/usr/bin/env swift
// Deterministic vector artwork for the Moss Display macOS app.
// Run from the repository root: swift host/macos/icon.swift
// Sandboxed regeneration: swift -module-cache-path /tmp/moss-icon-swift-cache host/macos/icon.swift
// Optional arguments: output.icns preview.png
import AppKit

func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor {
    return CGColor(red: r / 255, green: g / 255, blue: b / 255, alpha: a)
}

let forest = color(14, 35, 35)
let forestLight = color(31, 66, 52)
let cream = color(249, 235, 196)
let face = color(231, 210, 166)
let brown = color(158, 119, 88)
let furLight = color(181, 144, 104)
let furDark = color(108, 77, 59)
let mask = color(91, 65, 49)
let mint = color(135, 205, 163)

func ellipse(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ w: CGFloat,
             _ h: CGFloat, _ fill: CGColor) {
    c.setFillColor(fill)
    c.fillEllipse(in: CGRect(x: x, y: y, width: w, height: h))
}

func stroke(_ c: CGContext, _ path: CGPath, _ fill: CGColor, _ width: CGFloat) {
    c.addPath(path)
    c.setStrokeColor(fill)
    c.setLineWidth(width)
    c.setLineCap(.round)
    c.setLineJoin(.round)
    c.strokePath()
}

func drawMoss(_ c: CGContext) {
    let tile = CGPath(roundedRect: CGRect(x: 64, y: 64, width: 896, height: 896),
                      cornerWidth: 194, cornerHeight: 194, transform: nil)
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: 10), blur: 20, color: color(0, 0, 0, 0.24))
    c.addPath(tile)
    c.setFillColor(forest)
    c.fillPath()
    c.restoreGState()

    c.saveGState()
    c.addPath(tile)
    c.clip()
    let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                              colors: [forestLight, forest] as CFArray,
                              locations: [0, 1])!
    c.drawLinearGradient(gradient, start: CGPoint(x: 340, y: 64),
                         end: CGPoint(x: 640, y: 960),
                         options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    c.restoreGState()
    stroke(c, tile, color(146, 181, 165, 0.13), 3)

    // One young leaf, springing from the sloth's crown.
    let leaf = CGMutablePath()
    leaf.move(to: CGPoint(x: 677, y: 345))
    leaf.addCurve(to: CGPoint(x: 851, y: 178), control1: CGPoint(x: 672, y: 212),
                  control2: CGPoint(x: 773, y: 169))
    leaf.addCurve(to: CGPoint(x: 677, y: 345), control1: CGPoint(x: 861, y: 296),
                  control2: CGPoint(x: 786, y: 366))
    leaf.closeSubpath()
    c.setFillColor(mint)
    c.addPath(leaf)
    c.fillPath()
    let leafFold = CGMutablePath()
    leafFold.move(to: CGPoint(x: 677, y: 345))
    leafFold.addCurve(to: CGPoint(x: 851, y: 178), control1: CGPoint(x: 778, y: 335),
                      control2: CGPoint(x: 821, y: 245))
    leafFold.addCurve(to: CGPoint(x: 677, y: 345), control1: CGPoint(x: 805, y: 248),
                      control2: CGPoint(x: 749, y: 310))
    leafFold.closeSubpath()
    c.addPath(leafFold)
    c.setFillColor(color(77, 155, 111))
    c.fillPath()
    let stem = CGMutablePath()
    stem.move(to: CGPoint(x: 637, y: 390))
    stem.addCurve(to: CGPoint(x: 818, y: 215), control1: CGPoint(x: 704, y: 322),
                  control2: CGPoint(x: 766, y: 282))
    stroke(c, stem, color(83, 157, 114), 13)

    // A broad head and quiet outline remain legible at Dock and menu sizes.
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: 17), blur: 28, color: color(0, 0, 0, 0.23))
    ellipse(c, 197, 263, 630, 598, furDark)
    c.restoreGState()
    ellipse(c, 204, 247, 616, 586, brown)
    ellipse(c, 225, 252, 574, 485, furLight)

    // Soft tufts break the head outline without turning into cat ears.
    let tuft = CGMutablePath()
    tuft.move(to: CGPoint(x: 421, y: 284))
    tuft.addCurve(to: CGPoint(x: 480, y: 230), control1: CGPoint(x: 437, y: 254),
                  control2: CGPoint(x: 455, y: 239))
    tuft.addCurve(to: CGPoint(x: 488, y: 266), control1: CGPoint(x: 478, y: 245),
                  control2: CGPoint(x: 480, y: 255))
    tuft.addCurve(to: CGPoint(x: 538, y: 234), control1: CGPoint(x: 501, y: 243),
                  control2: CGPoint(x: 519, y: 237))
    tuft.addCurve(to: CGPoint(x: 551, y: 284), control1: CGPoint(x: 532, y: 253),
                  control2: CGPoint(x: 541, y: 271))
    tuft.closeSubpath()
    c.addPath(tuft)
    c.setFillColor(furLight)
    c.fillPath()

    let heartFace = CGMutablePath()
    heartFace.move(to: CGPoint(x: 512, y: 397))
    heartFace.addCurve(to: CGPoint(x: 269, y: 504), control1: CGPoint(x: 431, y: 309),
                       control2: CGPoint(x: 263, y: 321))
    heartFace.addCurve(to: CGPoint(x: 512, y: 763), control1: CGPoint(x: 265, y: 689),
                       control2: CGPoint(x: 359, y: 763))
    heartFace.addCurve(to: CGPoint(x: 755, y: 504), control1: CGPoint(x: 665, y: 763),
                       control2: CGPoint(x: 759, y: 689))
    heartFace.addCurve(to: CGPoint(x: 512, y: 397), control1: CGPoint(x: 761, y: 321),
                       control2: CGPoint(x: 593, y: 309))
    heartFace.closeSubpath()
    c.addPath(heartFace)
    c.setFillColor(face)
    c.fillPath()
    ellipse(c, 417, 526, 190, 154, cream)

    // Sloths' signature diagonal eye masks, mirrored left and right.
    for side in [-1.0, 1.0] {
        c.saveGState()
        c.translateBy(x: 512 + side * 138, y: 513)
        c.rotate(by: CGFloat(side) * .pi / 6)
        ellipse(c, -93, -65, 186, 130, mask)
        c.restoreGState()
        let eyeX = CGFloat(512 + side * 123)
        ellipse(c, eyeX - 20, 477, 40, 46, forest)
        ellipse(c, eyeX - 9, 484, 12, 13, cream)
        ellipse(c, eyeX + 6, 505, 5, 5, color(231, 210, 166, 0.65))
    }
    ellipse(c, 480, 563, 64, 43, mask)
    ellipse(c, 492, 569, 20, 8, color(181, 144, 104, 0.45))
    let smile = CGMutablePath()
    smile.move(to: CGPoint(x: 462, y: 633))
    smile.addCurve(to: CGPoint(x: 562, y: 633), control1: CGPoint(x: 481, y: 665),
                   control2: CGPoint(x: 543, y: 665))
    stroke(c, smile, mask, 14)
    ellipse(c, 337, 608, 42, 20, color(195, 145, 110, 0.36))
    ellipse(c, 645, 608, 42, 20, color(195, 145, 110, 0.36))
}

func pngData(size: Int) throws -> Data {
    // Render vector paths at each icon resolution, retaining real alpha corners.
    let info = CGImageAlphaInfo.premultipliedLast.rawValue
    guard let context = CGContext(data: nil, width: size, height: size,
                                  bitsPerComponent: 8, bytesPerRow: size * 4,
                                  space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: info) else {
        throw NSError(domain: "MossIcon", code: 1)
    }
    context.translateBy(x: 0, y: CGFloat(size))
    context.scaleBy(x: CGFloat(size) / 1024, y: -CGFloat(size) / 1024)
    context.setAllowsAntialiasing(true)
    context.setShouldAntialias(true)
    drawMoss(context)
    guard let image = context.makeImage(),
          let data = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:]) else {
        throw NSError(domain: "MossIcon", code: 2)
    }
    return data
}

let output = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "host/macos/MossDisplay.icns")
let preview = URL(fileURLWithPath: CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "artifacts/icon-v1.8.png")
let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("moss-icon-\(UUID().uuidString)", isDirectory: true)
let iconset = temporary.appendingPathComponent("MossDisplay.iconset", isDirectory: true)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: temporary) }
for base in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = "icon_\(base)x\(base)\(scale == 2 ? "@2x" : "").png"
        try pngData(size: base * scale).write(to: iconset.appendingPathComponent(name), options: .atomic)
    }
}
try FileManager.default.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
try FileManager.default.createDirectory(at: preview.deletingLastPathComponent(), withIntermediateDirectories: true)
try pngData(size: 1024).write(to: preview, options: .atomic)
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", output.path]
try iconutil.run()
iconutil.waitUntilExit()
if iconutil.terminationStatus != 0 {
    // Some macOS builds reject otherwise valid RGBA iconsets. ICNS also accepts
    // standard PNG chunks directly; preserve every 1x/2x size without conversion.
    func bigEndian(_ number: Int) -> Data {
        var value = UInt32(number).bigEndian
        return withUnsafeBytes(of: &value) { Data($0) }
    }
    let formats: [(String, Int)] = [
        ("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128),
        ("ic08", 256), ("ic09", 512), ("ic10", 1024),
        ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)
    ]
    var chunks = Data()
    for (type, size) in formats {
        let png = try pngData(size: size)
        chunks.append(contentsOf: type.utf8)
        chunks.append(bigEndian(png.count + 8))
        chunks.append(png)
    }
    var container = Data("icns".utf8)
    container.append(bigEndian(chunks.count + 8))
    container.append(chunks)
    try container.write(to: output, options: .atomic)
    print("Used standard PNG-backed ICNS container fallback")
}
guard let loaded = NSImage(contentsOf: output), loaded.isValid else {
    throw NSError(domain: "MossIcon", code: 4,
                  userInfo: [NSLocalizedDescriptionKey: "Generated icon failed AppKit validation"])
}
print("Created \(output.path)")
print("Preview \(preview.path)")
