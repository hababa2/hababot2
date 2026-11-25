#pragma once

#include <thread>
#include <mutex>
#include <functional>
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
	static void Shutdown();
	static bool Update();
	static void Render();
	static void RenderModalFrame();

	static void Close();
	static void Minimize();
	static bool Resizing();
	static void SaveWindowPlacement();
	static bool LoadWindowPlacement();
	static void SetRenderCallback(std::function<void()> callback);
	static void EnableNativeTitleBarDrag(void* platformHandle);
	static void MinimizeWindow(void* platformHandle);

	static std::string OpenFileBrowser();
	static std::string OpenFolderBrowser();

	static ID3D11Device* GetDevice();
	static ID3D11DeviceContext* GetContext();

	static bool resizing;
	
private:
	static void CreateDeviceD3D();
	static long long __stdcall WindowsMessageProc(HWND__* hWnd, unsigned int msg, unsigned long long wParam, long long lParam);

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
	static bool focused;
	static bool minimised;

	static std::function<void()> renderCallback;
	static bool inModalRender;

	static ID3D11Device* pd3dDevice;
	static ID3D11DeviceContext* pd3dDeviceContext;
	static IDXGISwapChain* pSwapChain;
	static ID3D11RenderTargetView* mainRenderTargetView;
};