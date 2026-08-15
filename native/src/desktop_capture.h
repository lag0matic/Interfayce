#pragma once

#include "desktop_surface_manager.h"

#include <openvr.h>
#include <d3d11.h>

#include <memory>

namespace interfayce {

class DesktopCapture {
public:
    enum class UpdateResult { NoFrame, FrameCopied, TextureChanged, Closed, Failed };

    DesktopCapture();
    ~DesktopCapture();
    DesktopCapture(const DesktopCapture&) = delete;
    DesktopCapture& operator=(const DesktopCapture&) = delete;

    bool Start(ID3D11Device* device, const DesktopSource& source);
    UpdateResult Update();
    vr::Texture_t Texture() const;
    float AspectRatio() const;
    int Width() const;
    int Height() const;
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace interfayce
