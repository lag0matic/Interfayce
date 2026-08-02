#include "desktop_capture.h"

#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <atomic>
#include <algorithm>
#include <wrl/client.h>

namespace {

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

GraphicsCaptureItem CreateCaptureItem(const interfayce::DesktopSource& source) {
    auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    HRESULT result = E_INVALIDARG;
    if (source.kind == interfayce::DesktopSource::Kind::Window && source.window != nullptr) {
        result = factory->CreateForWindow(source.window, winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item));
    } else if (source.kind == interfayce::DesktopSource::Kind::Display && source.monitor != nullptr) {
        result = factory->CreateForMonitor(source.monitor, winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item));
    }
    if (FAILED(result)) return nullptr;
    return item;
}

IDirect3DDevice CreateWinRtDevice(ID3D11Device* device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) return nullptr;
    winrt::com_ptr<IInspectable> inspectable;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()))) return nullptr;
    return inspectable.as<IDirect3DDevice>();
}

}  // namespace

namespace interfayce {

struct DesktopCapture::Impl {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
    IDirect3DDevice winRtDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frameToken{};
    winrt::event_token closedToken{};
    std::atomic_bool framePending{};
    std::atomic_bool closed{};
    HANDLE sharedHandle{};
    int width{};
    int height{};

    bool CreateTarget(int requestedWidth, int requestedHeight) {
        width = (std::max)(requestedWidth, 1);
        height = (std::max)(requestedHeight, 1);
        target.Reset();
        sharedHandle = nullptr;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &target))) return false;
        Microsoft::WRL::ComPtr<IDXGIResource> sharedResource;
        return SUCCEEDED(target.As(&sharedResource))
            && SUCCEEDED(sharedResource->GetSharedHandle(&sharedHandle));
    }
};

DesktopCapture::DesktopCapture() : impl_(std::make_unique<Impl>()) {}

DesktopCapture::~DesktopCapture() {
    Stop();
}

bool DesktopCapture::Start(ID3D11Device* device, const DesktopSource& source) {
    Stop();
    if (device == nullptr || !GraphicsCaptureSession::IsSupported()) return false;
    try {
        impl_->device = device;
        device->GetImmediateContext(&impl_->context);
        impl_->winRtDevice = CreateWinRtDevice(device);
        impl_->item = CreateCaptureItem(source);
        if (!impl_->winRtDevice || !impl_->item) return false;
        const auto size = impl_->item.Size();
        if (!impl_->CreateTarget(size.Width, size.Height)) return false;
        impl_->framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(impl_->winRtDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        impl_->frameToken = impl_->framePool.FrameArrived([state = impl_.get()](auto const&, auto const&) {
            state->framePending.store(true, std::memory_order_release);
        });
        impl_->closedToken = impl_->item.Closed([state = impl_.get()](auto const&, auto const&) {
            state->closed.store(true, std::memory_order_release);
        });
        impl_->session = impl_->framePool.CreateCaptureSession(impl_->item);
        impl_->session.IsCursorCaptureEnabled(false);
        impl_->session.StartCapture();
        return true;
    } catch (...) {
        Stop();
        return false;
    }
}

DesktopCapture::UpdateResult DesktopCapture::Update() {
    if (impl_->closed.load(std::memory_order_acquire)) return UpdateResult::Closed;
    if (!impl_->framePool || !impl_->framePending.exchange(false, std::memory_order_acq_rel)) {
        return UpdateResult::NoFrame;
    }
    try {
        const auto frame = impl_->framePool.TryGetNextFrame();
        if (!frame) return UpdateResult::NoFrame;
        const auto contentSize = frame.ContentSize();
        const bool sizeChanged = contentSize.Width != impl_->width || contentSize.Height != impl_->height;
        if (sizeChanged) {
            const winrt::Windows::Graphics::SizeInt32 normalizedSize{
                (std::max)(contentSize.Width, 1), (std::max)(contentSize.Height, 1)};
            frame.Close();
            if (!impl_->CreateTarget(normalizedSize.Width, normalizedSize.Height)) return UpdateResult::Failed;
            impl_->framePool.Recreate(impl_->winRtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2, normalizedSize);
            return UpdateResult::TextureChanged;
        }
        auto access = frame.Surface().as<
            ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(&sourceTexture)))) return UpdateResult::Failed;
        impl_->context->CopyResource(impl_->target.Get(), sourceTexture.Get());
        impl_->context->Flush();
        return UpdateResult::FrameCopied;
    } catch (...) {
        return UpdateResult::Failed;
    }
}

vr::Texture_t DesktopCapture::Texture() const {
    return {impl_->sharedHandle, vr::TextureType_DXGISharedHandle, vr::ColorSpace_Auto};
}

float DesktopCapture::AspectRatio() const {
    return impl_->height > 0 ? static_cast<float>(impl_->width) / static_cast<float>(impl_->height) : 1.0F;
}

void DesktopCapture::Stop() {
    if (!impl_) return;
    try {
        if (impl_->framePool && impl_->frameToken.value != 0) impl_->framePool.FrameArrived(impl_->frameToken);
        if (impl_->item && impl_->closedToken.value != 0) impl_->item.Closed(impl_->closedToken);
        if (impl_->session) impl_->session.Close();
        if (impl_->framePool) impl_->framePool.Close();
    } catch (...) {
    }
    impl_->session = nullptr;
    impl_->framePool = nullptr;
    impl_->item = nullptr;
    impl_->winRtDevice = nullptr;
    impl_->target.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->framePending = false;
    impl_->closed = false;
    impl_->sharedHandle = nullptr;
    impl_->width = 0;
    impl_->height = 0;
    impl_->frameToken = {};
    impl_->closedToken = {};
}

}  // namespace interfayce
