#pragma once
#include <opencv2/opencv.hpp>
#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class ScreenCapture {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* deskDupl = nullptr;
    bool initialized = false;
    bool CaptureDxgi(cv::Mat& outFrame);
    bool CaptureGDI(cv::Mat& outFrame);
public:
    bool Initialize();
    bool Capture(cv::Mat& outFrame);
    void Release();
    bool IsInitialized() const { return initialized; }
};