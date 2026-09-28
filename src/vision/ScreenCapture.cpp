#include "Vision/ScreenCapture.hpp"
#include "Logger.hpp"

bool ScreenCapture::Initialize() {
    try {
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
        if (FAILED(hr)) {
            Logger::Log("D3D11CreateDevice failed, will use GDI only");
            initialized = false;
            return true;
        }
        IDXGIDevice* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (FAILED(hr)) {
            Logger::Log("QueryInterface IDXGIDevice failed");
            return true;
        }
        IDXGIAdapter* adapter = nullptr;
        hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&adapter);
        dxgiDevice->Release();
        if (FAILED(hr)) {
            Logger::Log("GetParent adapter failed");
            return true;
        }
        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(0, &output);
        adapter->Release();
        if (FAILED(hr)) {
            Logger::Log("EnumOutputs failed");
            return true;
        }
        IDXGIOutput1* output1 = nullptr;
        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
        output->Release();
        if (FAILED(hr)) {
            Logger::Log("QueryInterface IDXGIOutput1 failed");
            return true;
        }
        hr = output1->DuplicateOutput(device, &deskDupl);
        output1->Release();
        if (FAILED(hr)) {
            Logger::Log("DuplicateOutput failed, will use GDI only");
            return true;
        }
        initialized = true;
        Logger::Log("ScreenCapture initialized (DXGI)");
        return true;
    } catch (const std::exception& e) {
        Logger::Log("ScreenCapture init exception: " + std::string(e.what()));
        initialized = false;
        return true;
    } catch (...) {
        Logger::Log("ScreenCapture init unknown exception");
        initialized = false;
        return true;
    }
}

bool ScreenCapture::CaptureDxgi(cv::Mat& outFrame) {
    if (!initialized || !deskDupl) return false;
    try {
        IDXGIResource* resource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        HRESULT hr = deskDupl->AcquireNextFrame(100, &frameInfo, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr)) {
            Logger::Log("AcquireNextFrame failed, disabling DXGI");
            initialized = false;
            if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
            return false;
        }
        ID3D11Texture2D* texture = nullptr;
        hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture);
        resource->Release();
        if (FAILED(hr)) {
            deskDupl->ReleaseFrame();
            return false;
        }
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
        if (FAILED(hr)) {
            texture->Release();
            deskDupl->ReleaseFrame();
            return false;
        }
        context->CopyResource(staging, texture);
        texture->Release();
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            staging->Release();
            deskDupl->ReleaseFrame();
            return false;
        }
        outFrame.create(desc.Height, desc.Width, CV_8UC3);
        BYTE* src = static_cast<BYTE*>(mapped.pData);
        for (UINT y = 0; y < desc.Height; ++y) {
            for (UINT x = 0; x < desc.Width; ++x) {
                BYTE* pixel = src + y * mapped.RowPitch + x * 4;
                cv::Vec3b& dst = outFrame.at<cv::Vec3b>(y, x);
                dst[0] = pixel[0]; dst[1] = pixel[1]; dst[2] = pixel[2];
            }
        }
        context->Unmap(staging, 0);
        staging->Release();
        deskDupl->ReleaseFrame();
        return true;
    } catch (...) {
        Logger::Log("CaptureDxgi exception");
        return false;
    }
}

bool ScreenCapture::CaptureGDI(cv::Mat& outFrame) {
    try {
        HDC hScreen = GetDC(nullptr);
        if (!hScreen) {
            Logger::Log("GetDC failed");
            return false;
        }
        HDC hDC = CreateCompatibleDC(hScreen);
        if (!hDC) {
            ReleaseDC(nullptr, hScreen);
            return false;
        }
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
        if (!hBitmap) {
            DeleteDC(hDC);
            ReleaseDC(nullptr, hScreen);
            return false;
        }
        HBITMAP hOld = (HBITMAP)SelectObject(hDC, hBitmap);
        BOOL ok = BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);
        SelectObject(hDC, hOld);
        if (!ok) {
            DeleteObject(hBitmap);
            DeleteDC(hDC);
            ReleaseDC(nullptr, hScreen);
            return false;
        }
        BITMAPINFOHEADER bi = { sizeof(bi), w, h, 1, 24, BI_RGB };
        outFrame.create(h, w, CV_8UC3);
        int ret = GetDIBits(hDC, hBitmap, 0, h, outFrame.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        DeleteObject(hBitmap);
        DeleteDC(hDC);
        ReleaseDC(nullptr, hScreen);
        return ret != 0;
    } catch (...) {
        Logger::Log("CaptureGDI exception");
        return false;
    }
}

bool ScreenCapture::Capture(cv::Mat& outFrame) {
    try {
        if (initialized && deskDupl) {
            if (CaptureDxgi(outFrame)) return true;
        }
        return CaptureGDI(outFrame);
    } catch (...) {
        Logger::Log("Capture exception");
        return false;
    }
}

void ScreenCapture::Release() {
    try {
        if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        initialized = false;
    } catch (...) {}
}