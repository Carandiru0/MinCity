#pragma once

#ifdef SPLASH_SCREEN_IMPLEMENTATION

namespace {

	namespace splash_screen
	{
		constinit static inline struct
		{
			HWND hWnd;
			HWND hWndOwner;

			HBITMAP hSplash;
			void* image_bits;
			
			Imaging splashi;
			Imaging splash0;
			Imaging splash1;
			void* hTimerThread;
			std::atomic_flag hTimerThreadCancel;
			tTime tStart;
			bool ok;

			static constexpr uint32_t const width{ 248 }, height{ 36 };
			constinit static inline wchar_t const* const szSplash = L"minCity";

		} splash_screen = {};

		static HWND const CreateSplashWindow(HINSTANCE const hInst)
		{
			splash_screen.hWndOwner = CreateWindow(splash_screen.szSplash, NULL, WS_POPUP, // hidden window, so that no taskbar item appears while only the splash screen appears
				0, 0, 0, 0, NULL, NULL, hInst, NULL);
			splash_screen.hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOPMOST, splash_screen.szSplash, NULL, WS_POPUP | WS_VISIBLE,
				0, 0, 0, 0, splash_screen.hWndOwner, NULL, hInst, NULL);

			return(splash_screen.hWnd);
		}
		static void RegisterWindowClass(HINSTANCE const hInst)
		{
			WNDCLASS wc = { 0 };
			wc.lpfnWndProc = DefWindowProc;
			wc.hInstance = hInst;
			wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(GLFW_ICON));
			wc.hCursor = LoadCursor(NULL, IDC_ARROW);
			wc.lpszClassName = splash_screen.szSplash;
			RegisterClass(&wc);
		}

		// https://faithlife.codes/blog/2008/09/displaying_a_splash_screen_with_c_part_i/
		static HBITMAP CreateHBITMAP(ImagingMemoryInstance const* const image, void*& pvImageBits)  // only supports rgb images
		{
			// initialize return value

			HBITMAP hbmp(nullptr);

			// get image attributes and check for valid image

			UINT const width = image->xsize;
			UINT const height = image->ysize;

			// prepare structure giving bitmap information (negative height indicates a top-down DIB)
			BITMAPINFO bminfo;
			ZeroMemory(&bminfo, sizeof(bminfo));
			bminfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bminfo.bmiHeader.biWidth = width;
			bminfo.bmiHeader.biHeight = -((LONG)height);
			bminfo.bmiHeader.biPlanes = 1;
			bminfo.bmiHeader.biBitCount = 32;
			bminfo.bmiHeader.biCompression = BI_RGB;

			// create a DIB section that can hold the image

			HDC hdcScreen = GetDC(nullptr);
			hbmp = CreateDIBSection(hdcScreen, &bminfo, DIB_RGB_COLORS, &pvImageBits, nullptr, 0);
			ReleaseDC(nullptr, hdcScreen);
			if (nullptr == hbmp)
				return(nullptr);

			// extract the image into the HBITMAP
			ImagingCopyRaw(pvImageBits, image);

			// caller! must call DeleteObject() on HBITMAP hbmp!!
			return(hbmp);
		}

		static bool const LoadSplashScreen()
		{
			Imaging imageSplash0L = ImagingLoadRawL(TEXTURE_DIR "mincity_0.8bit", splash_screen.width, splash_screen.height);
			Imaging imageSplash1L = ImagingLoadRawL(TEXTURE_DIR "mincity_1.8bit", splash_screen.width, splash_screen.height);

			if (imageSplash0L && imageSplash1L) {

				Imaging imageSplash0(nullptr);
				Imaging imageSplash1(nullptr);

				imageSplash0 = ImagingNew(MODE_BGRX, splash_screen.width, splash_screen.height);
				imageSplash1 = ImagingNew(MODE_BGRX, splash_screen.width, splash_screen.height);

				if (imageSplash0 && imageSplash1) {

					Parallel_Gray2BGRX(imageSplash0->image, imageSplash0L->image, splash_screen.width, splash_screen.height);
					Parallel_Gray2BGRX(imageSplash1->image, imageSplash1L->image, splash_screen.width, splash_screen.height);

					ImagingDelete(imageSplash0L); imageSplash0L = nullptr;
					ImagingDelete(imageSplash1L); imageSplash1L = nullptr;

					if (imageSplash0->xsize == imageSplash1->xsize
						&& imageSplash0->ysize == imageSplash1->ysize)
					{
						splash_screen.splashi = ImagingNew(MODE_BGRX, splash_screen.width, splash_screen.height);

						if (splash_screen.splashi) {
							splash_screen.splash0 = imageSplash0; imageSplash0 = nullptr;
							splash_screen.splash1 = imageSplash1; imageSplash1 = nullptr;

							return(true);
						}
					}
					else {

						ImagingDelete(imageSplash0); imageSplash0 = nullptr;
						ImagingDelete(imageSplash1); imageSplash1 = nullptr;
					}
				}
			}

			return(false);
		}

		static ImagingMemoryInstance const* const CurrentSplashScreen(float const tInterp)
		{
			if (splash_screen.splash0 && splash_screen.splash1) {

				ImagingLerp(splash_screen.splashi, splash_screen.splash0, splash_screen.splash1, tInterp);

				if (nullptr == splash_screen.hSplash) {

					splash_screen.hSplash = CreateHBITMAP(splash_screen.splashi, splash_screen.image_bits);
				}
				else {
					
					if (splash_screen.image_bits) {
						// extract the image into the HBITMAP
						ImagingCopyRaw(splash_screen.image_bits, splash_screen.splashi);
					}

				}

				return(splash_screen.splashi);
			}

			return(nullptr);
		}


		// Calls UpdateLayeredWindow to set a bitmap (with alpha) as the content of the splash window.
		static void SetSplashImage(HWND const hwndSplash, HBITMAP const hbmpSplash)
		{
			constinit static MONITORINFO monitorinfo = {};

			SIZE sizeSplash = { splash_screen.width, splash_screen.height };

			// get the primary monitor's info
			POINT ptZero = { 0 };
			if (0 == monitorinfo.cbSize)
			{
				HMONITOR hmonPrimary = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
				monitorinfo.cbSize = sizeof(monitorinfo);
				GetMonitorInfo(hmonPrimary, &monitorinfo);
			}

			// center the splash screen in the middle of the primary work area

			const RECT& rcWork = monitorinfo.rcWork;
			POINT ptOrigin;
			ptOrigin.x = rcWork.left + ((rcWork.right - rcWork.left - sizeSplash.cx) >> 1);
			ptOrigin.y = rcWork.top + ((rcWork.bottom - rcWork.top - sizeSplash.cy) >> 1);

			// create a memory DC holding the splash bitmap

			HDC hdcScreen = GetDC(NULL);
			HDC hdcMem = CreateCompatibleDC(hdcScreen);
			HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmpSplash);

			// use the source image's alpha channel for blending

			BLENDFUNCTION blend = { 0 };
			blend.BlendOp = AC_SRC_OVER;
			blend.SourceConstantAlpha = 255;
			blend.AlphaFormat = AC_SRC_ALPHA;

			// paint the window (in the right location) with the alpha-blended bitmap
			BOOL const hr = UpdateLayeredWindow(hwndSplash, hdcScreen, &ptOrigin, &sizeSplash,
				hdcMem, &ptZero, (COLORREF)0, &blend, ULW_ALPHA);

			// delete temporary objects
			SelectObject(hdcMem, hbmpOld);
			DeleteDC(hdcMem);
			ReleaseDC(NULL, hdcScreen);

			splash_screen.ok = (bool)hr;
		}

		static void UpdateSplashScreen(float const tProgress)
		{
			if (CurrentSplashScreen(tProgress) && splash_screen.hWnd && splash_screen.hSplash) {

				//UpdateWindow(splash_screen.hWnd);
				SetSplashImage(splash_screen.hWnd, splash_screen.hSplash);
			}
		}
		static void KillTimer() {
			if (splash_screen.hTimerThread) {

				splash_screen.hTimerThreadCancel.clear();
			}
		}
		__declspec(noinline) static void Release() {

			KillTimer();

			if (splash_screen.hSplash) {

				DeleteObject(splash_screen.hSplash);
				splash_screen.hSplash = nullptr;
			}

			if (splash_screen.splashi) {
				ImagingDelete(splash_screen.splashi); splash_screen.splashi = nullptr;
			}
			if (splash_screen.splash0) {
				ImagingDelete(splash_screen.splash0); splash_screen.splash0 = nullptr;
			}
			if (splash_screen.splash1) {
				ImagingDelete(splash_screen.splash1); splash_screen.splash1 = nullptr;
			}

			if (splash_screen.hWnd) {

				DestroyWindow(splash_screen.hWnd); splash_screen.hWnd = nullptr;
			}
			if (splash_screen.hWndOwner) {

				DestroyWindow(splash_screen.hWndOwner); splash_screen.hWndOwner = nullptr;
			}
		}

	} // end ns

	namespace splash_screen {

		static void __cdecl background_thread(void*)
		{
			while (splash_screen.ok && splash_screen.hTimerThreadCancel.test_and_set()) {

				static constexpr fp_seconds const
					timeMin(fp_seconds(milliseconds(33))),
					timeMax(fp_seconds(milliseconds(4000)));

				constinit static fp_seconds accumulator{ zero_time_duration };
				static tTime tLast{ splash_screen::splash_screen.tStart };

				tTime const tNow(std::chrono::steady_clock::now());
				
				fp_seconds const tDeltaLong(tNow - splash_screen::splash_screen.tStart);
				bool bKill(false),
					 bUpdate(false);

				float fTDeltaNormalized = tDeltaLong.count() / timeMax.count();
				if (fTDeltaNormalized > 1.0f) {
					fTDeltaNormalized = 1.0f;
					bKill = true;
					bUpdate = true;
				}

				if (!bUpdate) {
					fp_seconds const tDelta(tNow - tLast);
					accumulator += tDelta;
					while (accumulator >= timeMin) {

						bUpdate = true;
						accumulator -= timeMin;
					}
				}

				if (bUpdate) {
					splash_screen::UpdateSplashScreen(fTDeltaNormalized);
					tLast = tNow;
				}

				if (bKill) {
					splash_screen::KillTimer();
				}
				else {
					Sleep(10);
				}
			}

			_endthread();
			splash_screen.hTimerThread = nullptr;
		}

		__declspec(noinline) static void StartTimer()
		{
			if (splash_screen.ok && splash_screen.hWnd) {
				splash_screen.tStart = now();
				splash_screen.hTimerThreadCancel.test_and_set();
				splash_screen.hTimerThread = (void* const)_beginthread(&background_thread, 0, nullptr);
			}
		}

	} // end ns

} // end ns


#endif


