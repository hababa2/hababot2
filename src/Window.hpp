#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>

struct HINSTANCE__;
struct HICON__;
struct HWND__;

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

class Window
{
public:
	static bool Initialize();
	static bool Update();
	static void Render();
	static void Shutdown();

	static void Close();
	static bool Resizing();
	static void SaveWindowPlacement(HWND__* hwnd, const char* filename = "window_config.json");
	static bool LoadWindowPlacement(HWND__* hwnd, const char* filename = "window_config.json");

	static std::string OpenFileBrowser();
	static std::string OpenFolderBrowser();

private:
	static void CreateDeviceD3D();
	static void WindowThreadLoop();
	static long long __stdcall WindowsMessageProc(HWND__* hWnd, unsigned int msg, unsigned long long wParam, long long lParam);

	static std::thread windowThread;
	static std::mutex initMutex;
	static std::condition_variable initCondition;
	static bool windowInitialized;
	static bool windowCreationSuccess;

	static int windowPositionX;
	static int windowPositionY;
	static unsigned int windowWidth;
	static unsigned int windowHeight;
	static unsigned int screenWidth;
	static unsigned int screenHeight;
	static unsigned int virtualScreenWidth;
	static unsigned int virtualScreenHeight;
	static unsigned long style;

	static HINSTANCE__* instance;
	static HWND__* hWnd;

	static HICON__* currentCursor;
	static HICON__* arrow;
	static HICON__* hand;
	static HICON__* IBeam;
	static HICON__* crosshair;
	static HICON__* sizeNS;
	static HICON__* sizeWE;
	static HICON__* sizeNESW;
	static HICON__* sizeNWSE;

	static double targetFrametime;
	static std::atomic<bool> running;
	static bool resizing;
	static bool focused;
	static bool minimised;

	static ID3D11Device* pd3dDevice;
	static ID3D11DeviceContext* pd3dDeviceContext;
	static IDXGISwapChain* pSwapChain;
	static ID3D11RenderTargetView* mainRenderTargetView;
};