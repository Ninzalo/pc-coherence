import AppKit
import Metal
import CoreVideo

/// One real NSWindow per host `window_id`, hosting a `CAMetalLayer` and
/// (when a Metal device exists) a `CVMetalTextureCache` for the
/// decoder's output pixel buffers (plan rule 6: no placeholder/dummy
/// surface).
///
/// Presentation is a single full-quad blit of the decoder texture into
/// the drawable. The pipeline state is compiled from an embedded MSL
/// source (`MTLDevice.makeLibrary(source:)`) so the build stays
/// Xcode/CLT-free; on machines with no Metal device (headless) the
/// surface still exists but presents nothing — pixel output is
/// environment-dependent, construction is always real.
final class VideoSurfaceView {
    let window: NSWindow
    let layer: CAMetalLayer
    /// Nil when no Metal device is available (headless) — then
    /// presentation is a no-op and `present` never renders.
    let textureCache: CVMetalTextureCache?
    private let device: MTLDevice?
    private let commandQueue: MTLCommandQueue?
    private let pipelineState: MTLRenderPipelineState?
    let windowID: String

    /// Minimal fullscreen-quad shader (vertex ID → corner + UV).
    private static let mslSource = """
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut {
        float4 position [[position]];
        float2 uv;
    };

    vertex VSOut vs_main(uint vid [[vertex_id]]) {
        VSOut out;
        float2 uv = float2(float(vid & 1), float(vid >> 1));
        out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        out.position.y = -out.position.y;
        out.uv = uv;
        return out;
    }

    fragment float4 fs_main(VSOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]]) {
        constexpr sampler s(coord::normalized, filter::linear);
        return tex.sample(s, in.uv);
    }
    """

    /// `title` is the announced window title (host data, rendered as-is;
    /// the caller passes the L10n fallback when the title is empty).
    init(windowID: String, title: String, size: NSSize) {
        self.windowID = windowID
        let device = MTLCreateSystemDefaultDevice()
        self.device = device

        let win = NSWindow(
            contentRect: NSRect(origin: .zero,
                                size: NSSize(width: max(size.width, 320),
                                             height: max(size.height, 240))),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false)
        win.title = title
        win.isReleasedWhenClosed = false
        win.center()

        let metalLayer = CAMetalLayer()
        metalLayer.framebufferOnly = true
        if let device {
            metalLayer.device = device
            metalLayer.pixelFormat = .bgra8Unorm
        }
        // CAMetalLayer is a CALayer, not a view: host it in a layer-backed
        // NSView and fill the content view with that container.
        let container = NSView()
        container.wantsLayer = true
        container.layer = metalLayer
        if let content = win.contentView {
            content.addSubview(container)
            container.translatesAutoresizingMaskIntoConstraints = false
            NSLayoutConstraint.activate([
                container.leadingAnchor.constraint(equalTo: content.leadingAnchor),
                container.trailingAnchor.constraint(equalTo: content.trailingAnchor),
                container.topAnchor.constraint(equalTo: content.topAnchor),
                container.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            ])
        }

        var cache: CVMetalTextureCache?
        if let device {
            let attrs: CFDictionary? = nil
            let allocator: CFAllocator? = nil
            CVMetalTextureCacheCreate(allocator, attrs, device, attrs, &cache)
        }
        self.textureCache = cache

        var pipelineState: MTLRenderPipelineState?
        if let device, let commandQueue = device.makeCommandQueue() {
            self.commandQueue = commandQueue
            // SDK 27.2: makeLibrary returns MTLLibrary and throws (no
            // error-string tuple like older Metal overlays).
            if let library = try? device.makeLibrary(source: Self.mslSource,
                                                      options: nil),
               let vertex = library.makeFunction(name: "vs_main"),
               let fragment = library.makeFunction(name: "fs_main") {
                let descriptor = MTLRenderPipelineDescriptor()
                descriptor.vertexFunction = vertex
                descriptor.fragmentFunction = fragment
                descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
                pipelineState = try? device.makeRenderPipelineState(descriptor: descriptor)
            }
        } else {
            self.commandQueue = nil
        }

        self.window = win
        self.layer = metalLayer
        self.pipelineState = pipelineState
        win.makeKeyAndOrderFront(nil)
    }

    /// Blit one decoded pixel buffer into the window (main thread).
    func present(pixelBuffer: CVPixelBuffer) {
        guard let commandQueue, let pipelineState,
              let drawable = layer.nextDrawable() else { return }
        guard let texture = makeTexture(from: pixelBuffer) else { return }

        let commandBuffer = commandQueue.makeCommandBuffer()
        guard let commandBuffer else { return }
        let passDescriptor = MTLRenderPassDescriptor()
        passDescriptor.colorAttachments[0].loadAction = .dontCare
        passDescriptor.colorAttachments[0].storeAction = .store
        passDescriptor.colorAttachments[0].texture = drawable.texture
        guard let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: passDescriptor)
        else { return }
        encoder.setRenderPipelineState(pipelineState)
        encoder.setFragmentTexture(texture, index: 0)
        encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        encoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }

    /// Decode a pixel buffer into a Metal texture through the
    /// `CVMetalTextureCache` (the §9 decoder-output surface).
    /// SDK 27.2: the texture out-type is imported as `CVMetalTexture`
    /// (a `CVPixelBuffer` alias) and `CreateTextureFromImage` takes
    /// (width, height, planeIndex) = (0,0,0) to use source dimensions.
    private func makeTexture(from pixelBuffer: CVPixelBuffer) -> MTLTexture? {
        guard let textureCache else { return nil }
        var texture: CVMetalTexture?
        let attrs: CFDictionary? = nil
        let allocator: CFAllocator? = nil
        let status = CVMetalTextureCacheCreateTextureFromImage(
            allocator, textureCache, pixelBuffer, attrs,
            MTLPixelFormat.bgra8Unorm, 0, 0, 0, &texture)
        guard status == kCVReturnSuccess, let texture else { return nil }
        return CVMetalTextureGetTexture(texture)
    }

    func close() {
        window.close()
    }
}
