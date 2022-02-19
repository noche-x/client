#include <array>
#include <csignal>
#include <fstream>
#include <memory>
#include <system_error>

#ifdef _WIN32
#include <WinSock2.h>  // always needs to be included before "Windows.h"
#include <Windows.h>
#endif

#ifndef _WIN32
#include <cxxabi.h>
#include <execinfo.h>
#include <sys/mman.h>
#endif

#define IMGUI_USER_CONFIG "gui/imconfig.h"
#include "game.hpp"
#include "hooks.hpp"
#include "networking/tcpSocket.hpp"
#include "structs.hpp"
#include "utils/settings.hpp"
#include "utils/utils.hpp"

static constexpr std::array handledSignals = {
    std::pair<int, std::string_view>{SIGABRT, "Abort signal from abort(3)"},
    std::pair<int, std::string_view>{SIGFPE, "Floating point exception"},
    std::pair<int, std::string_view>{SIGILL, "Illegal Instruction"},
    std::pair<int, std::string_view>{SIGSEGV, "Invalid memory reference"},
// doesn't exist on windows
#ifndef _WIN32
    std::pair<int, std::string_view>{SIGBUS, "Bus error (bad memory access)"},
    std::pair<int, std::string_view>{SIGIOT, "IOT trap. A synonym for SIGABRT"},
    std::pair<int, std::string_view>{SIGQUIT, "Quit from keyboard"},
    std::pair<int, std::string_view>{SIGSYS, "Bad argument to routine (SVr4)"},
    std::pair<int, std::string_view>{SIGTRAP, "Trace/breakpoint trap"},
    std::pair<int, std::string_view>{SIGXCPU,
                                     "CPU time limit exceeded (4.2BSD)"},
    std::pair<int, std::string_view>{SIGXFSZ,
                                     "File size limit exceeded (4.2BSD)"},
#endif
};

static void atexitHandler() {
	// crashes maybe due to some invalidation shiße
	// spdlog::info("Exiting Sub Rosa, Goodbye!");
}

#ifndef _WIN32
static void signalHandler(int signal, siginfo_t* info, void* ucontext) {
	std::cerr << std::flush;
	std::cout << std::flush;

	std::string_view def = "Unknown";
	for (auto&& item : handledSignals) {
		if (item.first == signal) {
			def = item.second;
			break;
		}
	}

	spdlog::error("Game Crash! {}, {}", strsignal(signal), def.data());
	spdlog::error("Stack traceback:");

	void* array[10];
	size_t size;

	size = backtrace(array, 10);
	auto stacktraceString = backtrace_symbols(array, size);
	// backtrace_symbols_fd(array, size, STDERR_FILENO);

	auto formatBacktrace = [](int index, std::string str) {
		std::string constructed = "";

		std::string mod = "";
		std::string fun = "";
		std::string adr = "";

		int open_pos = str.find_first_of("(");
		mod = str.substr(0, open_pos);

		int plus_pos = str.find_first_of("+", open_pos);
		fun = str.substr(open_pos + 1, plus_pos - open_pos - 1);
		if (fun.length() > 0) {
			int status;

			std::size_t sz = 256;
			char* buffer = static_cast<char*>(std::malloc(sz));

			abi::__cxa_demangle(fun.data(), buffer, &sz, &status);

			if (status == 0)
				fun = buffer;
			else if (status == -1)
				spdlog::error(
				    "Error while demangling, a memory allocation failure occurred.");
			else if (status == -2)
				spdlog::error(
				    "Error while demangling, mangled_name is not a valid name under "
				    "the C++ ABI mangling rules.");
			else if (status == -3)
				spdlog::error(
				    "Error while demangling, one of the arguments is invalid.");
		}
		int open_box_pos = str.find_first_of("[");
		int close_box_pos = str.find_first_of("]", open_box_pos);
		adr = str.substr(open_box_pos + 1, close_box_pos - open_box_pos - 1);

		constructed = "#" + std::to_string(index) + "\t" + "Module \"" +
		              mod.c_str() + "\"" + ", at " + adr.c_str() + ", in " +
		              fun.c_str();
		return constructed;
	};

	std::vector<std::string> backtracePretty = {};

	for (int i = 0; i < size; i++)
		backtracePretty.push_back(
		    formatBacktrace(i, std::string(stacktraceString[i])));

	for (auto&& item : backtracePretty) spdlog::error("  {}", item.c_str());

	// void* backtraceEntries[10];

	// size_t backtraceSize = backtrace(backtraceEntries, 10);
	// auto backtraceSymbols = backtrace_symbols(backtraceEntries, backtraceSize);

	// for (int i = 0; i < backtraceSize; i++) {
	// std::cerr << "\t#" << i << ' ' << backtraceSymbols[i] << '\n';
	// }

	std::raise(signal);
	_exit(EXIT_FAILURE);
}
#endif

#ifdef _WIN32
BOOL WINAPI DllMain(_In_ HINSTANCE hinstDll, _In_ DWORD fdwReason,
                    _In_opt_ LPVOID lpvReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
		if (!AllocConsole()) {
			ERROR_AND_EXIT("Failed to allocate console.");
			return false;
		}

		_iobuf* data;
		const errno_t res = freopen_s(&data, "CONOUT$", "w", stdout);
		if (res != 0) {
			ERROR_AND_EXIT(spdlog::fmt_lib::format(
			    "Failed to open stdout filestream. Error code: %i (%s)", res,
			    std::system_category().message(res)));
			return false;
		}

		if (!SetConsoleTitleA("Sub Rosa: Custom Console")) {
			ERROR_AND_EXIT("Failed to set console title.");
			return false;
		}

		WSAData wsaData;

		if (int result = WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
			throw std::runtime_error("Failed to intalize WSA");
		}
#else

void __attribute__((constructor)) entry() {
#endif
		std::atexit(&atexitHandler);
#ifndef _WIN32
		struct sigaction sigact;

		sigact.sa_sigaction = signalHandler;
		sigact.sa_flags = SA_RESTART | SA_SIGINFO;

		for (auto&& sig : handledSignals) {
			sighandler_t handler;

			if (sigaction(sig.first, &sigact, nullptr) != 0) {
				throw std::runtime_error(spdlog::fmt_lib::format(
				    "Error while assigning signal handler for {} ({})", sig.first,
				    strsignal(sig.first)));
			}
		}
		std::signal(SIGPIPE, SIG_IGN);
#endif

		g_utils = std::make_unique<utils>(spdlog::level::info);

		spdlog::set_error_handler([](const std::string& msg) {
			std::cout << "spdlog has errored, " << msg << std::endl;
		});

		g_settings = std::make_unique<settings>();
		g_game = std::make_unique<game>();
		spdlog::info("Base address: {:#x}", g_game->getBaseAddress());
		g_hooks = std::make_unique<hooks>();
		g_hooks->install();
		spdlog::info("Initalization complete.");

#ifdef _WIN32
	}
	return TRUE;
#endif
}