#include "Window.hpp"

#include "Badges.hpp"
#include "Emotes.hpp"

#include "../resource.h"

#include "json.hpp"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <iostream>
#include <fstream>

#include <d3d11.h>

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shobjidl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")

std::thread Window::windowThread;
std::mutex Window::initMutex;
std::condition_variable Window::initCondition;
bool Window::windowInitialized;
bool Window::windowCreationSuccess;

int Window::windowPositionX;
int Window::windowPositionY;
unsigned int Window::windowWidth;
unsigned int Window::windowHeight;
unsigned int Window::screenWidth;
unsigned int Window::screenHeight;
unsigned int Window::virtualScreenWidth;
unsigned int Window::virtualScreenHeight;
unsigned long Window::style;

HINSTANCE__* Window::instance;
HWND__* Window::hWnd;

HICON__* Window::currentCursor;
HICON__* Window::arrow;
HICON__* Window::hand;
HICON__* Window::IBeam;
HICON__* Window::crosshair;
HICON__* Window::sizeNS;
HICON__* Window::sizeWE;
HICON__* Window::sizeNESW;
HICON__* Window::sizeNWSE;

double Window::targetFrametime;
std::atomic<bool> Window::running;
bool Window::resizing;
bool Window::focused;
bool Window::minimised;

ID3D11Device* Window::pd3dDevice;
ID3D11DeviceContext* Window::pd3dDeviceContext;
IDXGISwapChain* Window::pSwapChain;
ID3D11RenderTargetView* Window::mainRenderTargetView;

bool Window::Initialize()
{
	instance = GetModuleHandleW(nullptr);

	arrow = LoadCursorW(nullptr, IDC_ARROW);
	hand = LoadCursorW(nullptr, IDC_HAND);
	IBeam = LoadCursorW(nullptr, IDC_IBEAM);
	crosshair = LoadCursorW(nullptr, IDC_CROSS);
	sizeNS = LoadCursorW(nullptr, IDC_SIZENS);
	sizeWE = LoadCursorW(nullptr, IDC_SIZEWE);
	sizeNESW = LoadCursorW(nullptr, IDC_SIZENESW);
	sizeNWSE = LoadCursorW(nullptr, IDC_SIZENWSE);
	currentCursor = arrow;

	style = WS_POPUP | WS_VISIBLE;

	screenWidth = GetSystemMetrics(SM_CXSCREEN);
	screenHeight = GetSystemMetrics(SM_CYSCREEN);
	virtualScreenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	virtualScreenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	DEVMODE monitorInfo{};
	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &monitorInfo);
	targetFrametime = 1.0 / monitorInfo.dmDisplayFrequency;

	running = true;

	windowThread = std::thread(WindowThreadLoop);
	std::unique_lock<std::mutex> lock(initMutex);
	initCondition.wait(lock, [&] { return windowInitialized; });

	if (!windowCreationSuccess)
	{
		running = false;
		if (windowThread.joinable()) windowThread.join();
		return false;
	}

	return true;
}

void Window::Shutdown()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (hWnd && running)
	{
		PostMessageW(hWnd, WM_CLOSE, 0, 0);
	}

	if (windowThread.joinable())
	{
		windowThread.join();
	}

	UnregisterClassW(L"Chat Viewer", instance);
	hWnd = nullptr;
}

bool Window::Update()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	return running;
}

void Window::Render()
{
	if (mainRenderTargetView != nullptr)
	{
		const float clearColorWithAlpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		pd3dDeviceContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
		pd3dDeviceContext->ClearRenderTargetView(mainRenderTargetView, clearColorWithAlpha);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		pSwapChain->Present(1, 0);
	}
}

void Window::WindowThreadLoop()
{
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowsMessageProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = instance;
	wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = nullptr;
	wc.hbrBackground = nullptr;
	wc.lpszMenuName = L"Chat Viewer";
	wc.lpszClassName = L"Chat Viewer";
	wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCE(IDI_ICON1));

	RegisterClassExW(&wc);

	hWnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, L"Chat Viewer", L"Chat Viewer", style,
		100, 100, 600, 600, nullptr, nullptr, instance, nullptr);

	if (hWnd)
	{
		MARGINS margins = { -1, -1, -1, -1 };
		DwmExtendFrameIntoClientArea(hWnd, &margins);

		if (!LoadWindowPlacement(hWnd))
		{
			ShowWindow(hWnd, SW_SHOW);
		}

		UpdateWindow(hWnd);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(hWnd);
		CreateDeviceD3D();
		ImGui_ImplDX11_Init(pd3dDevice, pd3dDeviceContext);
	}

	{
		std::lock_guard<std::mutex> lock(initMutex);
		windowCreationSuccess = (hWnd != nullptr);
		windowInitialized = true;
	}
	initCondition.notify_one();

	if (!windowCreationSuccess) { return; }

	MSG msg;
	int result;

	while (running && (result = GetMessageW(&msg, nullptr, 0, 0)) != 0)
	{
		if (result == -1) { break; }

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	running = false;
}

void Window::CreateDeviceD3D()
{
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

	if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &pSwapChain, &pd3dDevice, &featureLevel, &pd3dDeviceContext) != S_OK) { return; }

	ID3D11Texture2D* pBackBuffer;
	pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
	pBackBuffer->Release();

	Badges::Initialize(pd3dDevice);
	Emotes::Initialize(pd3dDevice);
}

void Window::Close()
{
	running = false;
}

bool Window::Resizing()
{
	return resizing;
}

void Window::SaveWindowPlacement(HWND hwnd, const char* filename)
{
	WINDOWPLACEMENT wp{};
	wp.length = sizeof(WINDOWPLACEMENT);

	if (GetWindowPlacement(hwnd, &wp))
	{
		nlohmann::json j = {
			{ "showCmd", wp.showCmd },
			{ "left",    wp.rcNormalPosition.left },
			{ "top",     wp.rcNormalPosition.top },
			{ "right",   wp.rcNormalPosition.right },
			{ "bottom",  wp.rcNormalPosition.bottom }
		};

		std::ofstream file(filename);
		if (file.is_open())
		{
			file << j.dump(4);
		}
	}
}

bool Window::LoadWindowPlacement(HWND hwnd, const char* filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) { return false; }

	auto j = nlohmann::json::parse(file, nullptr, false);
	if (j.is_discarded()) { return false; }

	WINDOWPLACEMENT wp{};
	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(hwnd, &wp);

	wp.showCmd = j.value("showCmd", SW_SHOWNORMAL);
	wp.rcNormalPosition.left = j.value("left", 100);
	wp.rcNormalPosition.top = j.value("top", 100);
	wp.rcNormalPosition.right = j.value("right", 500);
	wp.rcNormalPosition.bottom = j.value("bottom", 700);

	if (wp.showCmd == SW_SHOWMINIMIZED)
	{
		wp.showCmd = SW_SHOWNORMAL;
	}

	return SetWindowPlacement(hwnd, &wp) != 0;
}

std::string Window::OpenFileBrowser()
{
	OPENFILENAMEA ofn;
	CHAR szFile[260] = { 0 };

	ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
	ofn.lStructSize = sizeof(OPENFILENAMEA);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;

	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		std::string selectedPath = ofn.lpstrFile;
		std::replace(selectedPath.begin(), selectedPath.end(), '\\', '/');
		return selectedPath;
	}

	return "";
}

std::string Window::OpenFolderBrowser()
{
	std::string result;

	HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
	{
		return result;
	}

	IFileOpenDialog* pDialog = nullptr;
	HRESULT hr = CoCreateInstance(
		CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));

	if (SUCCEEDED(hr) && pDialog)
	{
		DWORD options = 0;
		if (SUCCEEDED(pDialog->GetOptions(&options)))
		{
			pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
		}

		if (SUCCEEDED(pDialog->Show(hWnd)))
		{
			IShellItem* pItem = nullptr;
			if (SUCCEEDED(pDialog->GetResult(&pItem)) && pItem)
			{
				PWSTR pszPath = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath)
				{
					int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, nullptr, 0, nullptr, nullptr);
					if (sizeNeeded > 1)
					{
						result.resize(sizeNeeded - 1);
						WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, result.data(), sizeNeeded, nullptr, nullptr);
						std::replace(result.begin(), result.end(), '\\', '/');
					}
					CoTaskMemFree(pszPath);
				}
				pItem->Release();
			}
		}
		pDialog->Release();
	}

	if (SUCCEEDED(hrInit))
	{
		CoUninitialize();
	}

	return result;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT __stdcall Window::WindowsMessageProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) { return true; }

	switch (msg)
	{
	case WM_CREATE: {} return 0;
	case WM_SETFOCUS: {
		focused = true;
	} return 0;
	case WM_KILLFOCUS: {
		focused = false;
	} return 0;
	case WM_QUIT: {
		focused = false;
		running = false;
	} return 0;
	case WM_CLOSE: {
		focused = false;
		running = false;
	} return 0;
	case WM_DESTROY: {
		focused = false;
		running = false;
		SaveWindowPlacement(hWnd);
		PostQuitMessage(0);
	} return 0;
	case WM_ERASEBKGND: {} return 1;
	case WM_ENTERSIZEMOVE: {
		resizing = true;
	} return 0;
	case WM_EXITSIZEMOVE:
	{
		resizing = false;

		if (pSwapChain != nullptr)
		{
			if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = nullptr; }

			RECT rc;
			GetClientRect(hWnd, &rc);
			UINT width = rc.right - rc.left;
			UINT height = rc.bottom - rc.top;

			pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

			ID3D11Texture2D* pBackBuffer = nullptr;
			pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
			if (pBackBuffer)
			{
				pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
				pBackBuffer->Release();
			}
		}
	} return 0;
	case WM_SIZE: {
		switch (wParam)
		{
		case SIZE_MINIMIZED: {
			focused = false;
			minimised = true;
		} return 0;
		case SIZE_RESTORED: {
			focused = true;
			minimised = false;
		} break;
		}

		if (pSwapChain != nullptr)
		{
			if (mainRenderTargetView)
			{
				mainRenderTargetView->Release();
				mainRenderTargetView = nullptr;
			}

			UINT width = (UINT)LOWORD(lParam);
			UINT height = (UINT)HIWORD(lParam);
			pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

			ID3D11Texture2D* pBackBuffer = nullptr;
			pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
			if (pBackBuffer)
			{
				pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &mainRenderTargetView);
				pBackBuffer->Release();
			}
		}
	} return 0;
	case WM_SIZING: {
	} return 1;
	case WM_MOVE: {
	} return 0;
	case WM_KEYDOWN: {
		if (wParam == VK_ESCAPE)
		{
			DestroyWindow(hWnd);
		}
	} return 0;
	case WM_NCHITTEST: {
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		RECT rc;
		GetWindowRect(hWnd, &rc);

		const int borderThickness = 8;
		const int dragZoneHeight = 30;

		if (pt.x < rc.left + borderThickness && pt.y < rc.top + borderThickness) { return HTTOPLEFT; }
		if (pt.x > rc.right - borderThickness && pt.y < rc.top + borderThickness) { return HTTOPRIGHT; }
		if (pt.x < rc.left + borderThickness && pt.y > rc.bottom - borderThickness) { return HTBOTTOMLEFT; }
		if (pt.x > rc.right - borderThickness && pt.y > rc.bottom - borderThickness) { return HTBOTTOMRIGHT; }

		if (pt.x < rc.left + borderThickness) { return HTLEFT; }
		if (pt.x > rc.right - borderThickness) { return HTRIGHT; }
		if (pt.y < rc.top + borderThickness) { return HTTOP; }
		if (pt.y > rc.bottom - borderThickness) { return HTBOTTOM; }

		POINT clientPt = pt;
		ScreenToClient(hWnd, &clientPt);
		if (clientPt.y < dragZoneHeight) { return HTCAPTION; }
	} break;
	case WM_SETCURSOR: {
		switch (LOWORD(lParam))
		{
		case HTCLIENT: { SetCursor(currentCursor); } return 1; //Client Area
			//case HTCAPTION: { SetCursor(arrow); } return 1; //Title Bar
			//case HTSYSMENU: { SetCursor(hand); } return 1; //Window Menu
			//case HTGROWBOX: { SetCursor(arrow); } return 1; //Size Box?
			//case HTMENU: { SetCursor(hand); } return 1; //Menu
			//case HTHSCROLL: { SetCursor(hand); } return 1; //Horizontal Scroll Bar
			//case HTVSCROLL: { SetCursor(hand); } return 1; //Vertical Scroll Bar
			//case HTMINBUTTON: { SetCursor(hand); } return 1; //Minimize Button
			//case HTMAXBUTTON: { SetCursor(hand); } return 1; //Maximize Button
			//case HTLEFT: { SetCursor(sizeWE); } return 1; //Border Left
			//case HTRIGHT: { SetCursor(sizeWE); } return 1; //Border Right
			//case HTTOP: { SetCursor(sizeNS); } return 1; //Border Top
			//case HTTOPLEFT: { SetCursor(sizeNWSE); } return 1; //Border Top Left
			//case HTTOPRIGHT: { SetCursor(sizeNESW); } return 1; //Border Top Right
			//case HTBOTTOM: { SetCursor(sizeNS); } return 1; //Border Bottom
			//case HTBOTTOMLEFT: { SetCursor(sizeNESW); } return 1; //Border Bottom Left
			//case HTBOTTOMRIGHT: { SetCursor(sizeNWSE); } return 1; //Border Bottom Right
			//case HTBORDER: { SetCursor(arrow); } return 1; //Any Border (Not Resizable)
			//case HTOBJECT: { SetCursor(); } return 1;
			//case HTCLOSE: { SetCursor(); } return 1;
			//case HTHELP: { SetCursor(); } return 1;
		default: break;
		}
	} break;
	}

	return DefWindowProcW(hWnd, msg, wParam, lParam);
}