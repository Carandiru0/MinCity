#include "pch.h"
#include <atomic>
#include "tTime.h"
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <iostream>
#include <filesystem>

#include "MinCity.h"

using namespace std;

#define _IO_NO_REDIRECTION -1
#define _IO_CONSOLE 1
#define _IO_FILE 0

#define _LOG_FILE_NAME L"mincity.log"

constinit static inline int _IsRedirectedTo{ _IO_NO_REDIRECTION };
constinit FILE* _streamFile{};

static constexpr uint32_t const MAX_CONSOLE_LINES = 8192;

typedef struct { char* _ptr; int _cnt; char* _base; int _flag; int _file; int _charbuf; int _bufsiz; char* _tmpfname; } FILE_COMPLETE;

__declspec(noinline) void RedirectIOToConsole()
{
	CONSOLE_SCREEN_BUFFER_INFO coninfo;

	// allocate a console for this app

	AllocConsole();

	// set the screen buffer to be big enough to let us scroll text
	HANDLE const hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	GetConsoleScreenBufferInfo(hOut,

		&coninfo);

	coninfo.dwSize.Y = MAX_CONSOLE_LINES;

	SetConsoleScreenBufferSize(hOut,

		coninfo.dwSize);

	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING); // enable ansi color !!! windows 10 only

	*(FILE_COMPLETE*)stdout = *(FILE_COMPLETE*)_wfdopen(_open_osfhandle((intptr_t)hOut, _O_TEXT), L"w");

	// this is important so that main application is not slowed down
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(NULL);

	_IsRedirectedTo = _IO_CONSOLE;
}

__declspec(noinline) void RedirectIOToFile()
{
	std::filesystem::path savePath(MinCity::getUserFolder());
	savePath += USER_DIR _LOG_FILE_NAME;

	if (0 == _wfreopen_s(&_streamFile, savePath.c_str(), L"w", stdout)) {

		// this is important so that main application is not slowed down
		std::ios_base::sync_with_stdio(false);
		std::cin.tie(NULL);

		_IsRedirectedTo = _IO_FILE;
	}
}

__declspec(noinline) void WaitIOClose()
{
	if (_IO_FILE == _IsRedirectedTo) {

		if (_streamFile) {
			fflush(stdout);
			// ** causes crash if handle is closed fclose(_streamFile); _streamFile = nullptr;
		}

	}
	else if (_IO_CONSOLE == _IsRedirectedTo) {

		static constexpr uint32_t const SLEEPTIME = 64U;
		static constexpr seconds const AUTOCLOSE_SECONDS = seconds(3U);

		std::atomic_bool timed_out;
		timed_out = false;

		tTime const tStart(high_resolution_clock::now());

		tbb::task_group tG;
		tG.run([&]
			{
				fmt::print(fg(fmt::color::red) | fmt::emphasis::underline, "\nPress Any key to close this window.... \n");
				FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
				while (!timed_out) {

					if (_kbhit()) {
						timed_out = true;
					}
					else if (!timed_out) { // quick exit if keyboard hit, sleep only when neccessary
						Sleep(SLEEPTIME);
					}
				}
			});

		do {
			tTime tNow(high_resolution_clock::now()); // real-time domain
			static tTime tLast(tNow);
			static bool clearedDotOn(true);
			static int64_t numOnDots(AUTOCLOSE_SECONDS.count());

			seconds const duration_timeout_remaining(AUTOCLOSE_SECONDS - duration_cast<seconds>(tNow - tStart));

			if (tNow - tLast >= seconds(1)) {
				tLast = tNow;

				uint64_t const seconds_remaining = duration_timeout_remaining.count();

				if (!clearedDotOn) {
					fmt::print(fg(fmt::color::gray), "\b");
					clearedDotOn = true;
				}
				numOnDots = (int64_t)seconds_remaining;

				while (--numOnDots >= 0) {
					fmt::print(fg(fmt::color::gray), ".");
				}

				fmt::print(fg(fmt::color::gray), "\n");
			}

			if (--numOnDots >= 0) {
				fmt::print(fg(fmt::color::white), ".");
				clearedDotOn = false;
			}

			if (seconds(0) == duration_timeout_remaining) {
				timed_out = true;
			}
			else { // quick exit on time out, sleep only when neccesary
				Sleep(SLEEPTIME);
			}

		} while (!timed_out);

		tG.wait();

		FreeConsole();
	}
}