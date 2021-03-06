﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2015 Chengr28
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Main.h"

//The Main function of program
#if defined(PLATFORM_WIN)
int wmain(int argc, wchar_t* argv[])
{
//Windows XP with SP3 support
	#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
		GetFunctionPointer(FUNCTION_GETTICKCOUNT64);
		GetFunctionPointer(FUNCTION_INET_NTOP);
	#endif
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
int main(int argc, char *argv[])
{
#endif

//Get commands.
	if (argc > 0)
	{
		if (!ReadCommand(argc, argv))
			return EXIT_SUCCESS;
	}
	else {
		return EXIT_FAILURE;
	}

//Read configuration file and WinPcap or LibPcap initialization.
	if (!ReadParameter())
	{
		WSACleanup();
		return EXIT_FAILURE;
	}

//Mark Local DNS address to PTR Records.
	std::thread NetworkInformationMonitorThread(NetworkInformationMonitor);
	NetworkInformationMonitorThread.detach();

//Read IPFilter and Hosts.
	if (Parameter.OperationMode == LISTEN_MODE_CUSTOM || Parameter.BlacklistCheck || Parameter.LocalRouting)
	{
		std::thread IPFilterThread(ReadIPFilter);
		IPFilterThread.detach();
	}

	std::thread HostsThread(ReadHosts);
	HostsThread.detach();

//DNSCurve initialization
#if defined(ENABLE_LIBSODIUM)
	if (Parameter.DNSCurve && DNSCurveParameter.IsEncryption)
	{
		randombytes_set_implementation(&randombytes_salsa20_implementation);
		randombytes_stir();
		DNSCurveInit();
	}
#endif

#if defined(PLATFORM_WIN)
//Service initialization and start service.
	SERVICE_TABLE_ENTRYW ServiceTable[] = {{DEFAULT_LOCAL_SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain}, {nullptr, nullptr}};
	if (!StartServiceCtrlDispatcherW(ServiceTable))
	{
		Parameter.Console = true;
		wprintf_s(L"System Error: Service start error, error code is %lu.\n", GetLastError());
		wprintf_s(L"System Error: Program will continue to run in console mode.\n");
		wprintf_s(L"Please ignore those error messages if you want to run in console mode.\n");

	//Handle the system signal and start all monitors.
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
		MonitorInit();
	}

#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	MonitorInit();
#endif

	WSACleanup();
	return EXIT_SUCCESS;
}

//Read commands from main program
#if defined(PLATFORM_WIN)
bool __fastcall ReadCommand(int argc, wchar_t* argv[])
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
bool ReadCommand(int argc, char *argv[])
#endif
{
//Path initialization
#if defined(PLATFORM_WIN)
	if (!FileNameInit(argv[0]))
		return false;
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	std::shared_ptr<char> FileName(new char[PATH_MAX + 1U]());
	memset(FileName.get(), 0, PATH_MAX + 1U);
	if (getcwd(FileName.get(), PATH_MAX) == nullptr)
	{
		wprintf(L"Path initialization error.\n");
		return false;
	}
	if (!FileNameInit(FileName.get()))
		return false;
	FileName.reset();
#endif

#if defined(PLATFORM_WIN)
//Winsock initialization
	std::shared_ptr<WSAData> WSAInitialization(new WSAData());
	if (WSAStartup(MAKEWORD(WINSOCK_VERSION_HIGH, WINSOCK_VERSION_LOW), WSAInitialization.get()) != 0 ||
		LOBYTE(WSAInitialization->wVersion) != WINSOCK_VERSION_LOW || HIBYTE(WSAInitialization->wVersion) != WINSOCK_VERSION_HIGH)
	{
		wprintf_s(L"Winsock initialization error, error code is %d.\n", WSAGetLastError());
		PrintError(LOG_ERROR_NETWORK, L"Winsock initialization error", WSAGetLastError(), nullptr, 0);

		WSACleanup();
		return false;
	}

//Read commands.
	std::wstring Commands;
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	std::string Commands;
#endif
	for (size_t Index = 1U;(SSIZE_T)Index < argc;++Index)
	{
		Commands = argv[Index];

	//Flush DNS Cache from user.
		if (Commands == COMMAND_FLUSH_DNS)
		{
		#if defined(PLATFORM_WIN)
			FlushDNSMailSlotSender();
		#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
			FlushDNSFIFOSender();
		#endif

			WSACleanup();
			return false;
		}
	//Windows Firewall Test in first start.
	#if defined(PLATFORM_WIN)
		else if (Commands == COMMAND_FIREWALL_TEST)
		{
			if (!FirewallTest(AF_INET6) && !FirewallTest(AF_INET))
			{
				wprintf_s(L"Windows Firewall Test error.\n");
				PrintError(LOG_ERROR_NETWORK, L"Windows Firewall Test error", WSAGetLastError(), nullptr, 0);
			}

			WSACleanup();
			return false;
		}
	#endif
	//Set system daemon.
	#if defined(PLATFORM_LINUX)
		else if (Commands == COMMAND_DISABLE_DAEMON)
		{
			Parameter.Daemon = false;
		}
	#endif
	//Print current version.
		else if (Commands == COMMAND_LONG_PRINT_VERSION || Commands == COMMAND_SHORT_PRINT_VERSION)
		{
			wprintf_s(L"Pcap_DNSProxy ");
			wprintf_s(FULL_VERSION);
			wprintf_s(L"\n");

			WSACleanup();
			return false;
		}
	//Print help messages.
		else if (Commands == COMMAND_LONG_HELP || Commands == COMMAND_SHORT_HELP)
		{
			wprintf_s(L"Usage: Please see ReadMe... files in Documents folder.\n");

			WSACleanup();
			return false;
		}
	//Set working directory from commands.
		else if (Commands == COMMAND_LONG_SET_PATH || Commands == COMMAND_SHORT_SET_PATH)
		{
		//Commands check
			if ((SSIZE_T)Index + 1 >= argc)
			{
				wprintf_s(L"Commands error.\n");
				PrintError(LOG_ERROR_SYSTEM, L"Commands error", 0, nullptr, 0);

				WSACleanup();
				return false;
			}
			else {
				++Index;
				Commands = argv[Index];

			//Path check.
				if (Commands.length() > MAX_PATH)
				{
					wprintf_s(L"Commands error.\n");
					PrintError(LOG_ERROR_SYSTEM, L"Commands error", 0, nullptr, 0);

					WSACleanup();
					return false;
				}
				else {
					if (!FileNameInit(Commands.c_str()))
						return false;
				}
			}
		}
	}

//Set system daemon.
#if defined(PLATFORM_LINUX)
	if (Parameter.Daemon && daemon(0, 0) == RETURN_ERROR)
	{
		PrintError(LOG_ERROR_SYSTEM, L"Set system daemon error", 0, nullptr, 0);
		return false;
	}
#endif

	return true;
}

//Get path of program from the main function parameter and Winsock initialization
#if defined(PLATFORM_WIN)
bool __fastcall FileNameInit(const wchar_t *OriginalPath)
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
bool FileNameInit(const char *OriginalPath)
#endif
{
//Path process
#if defined(PLATFORM_WIN)
	Parameter.Path_Global->clear();
	Parameter.Path_Global->push_back(OriginalPath);
	Parameter.Path_Global->front().erase(Parameter.Path_Global->front().rfind(L"\\") + 1U);
	for (size_t Index = 0;Index < Parameter.Path_Global->front().length();++Index)
	{
		if ((Parameter.Path_Global->front()).at(Index) == L'\\')
		{
			Parameter.Path_Global->front().insert(Index, L"\\");
			++Index;
		}
	}
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	Parameter.sPath_Global->clear();
	Parameter.sPath_Global->push_back(OriginalPath);
	Parameter.sPath_Global->front().append("/");
	std::wstring StringTemp;
	MBSToWCSString(StringTemp, OriginalPath);
	StringTemp.append(L"/");
	Parameter.Path_Global->clear();
	Parameter.Path_Global->push_back(StringTemp);
	StringTemp.clear();
#endif

//Get path of error/running status log file and mark start time.
	Parameter.Path_ErrorLog->clear();
	*Parameter.Path_ErrorLog = Parameter.Path_Global->front();
	Parameter.Path_ErrorLog->append(L"Error.log");
#if (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	Parameter.sPath_ErrorLog->clear();
	*Parameter.sPath_ErrorLog = Parameter.sPath_Global->front();
	Parameter.sPath_ErrorLog->append("Error.log");
#endif
	Parameter.PrintError = true;
	time(&StartTime);

	return true;
}

#if defined(PLATFORM_WIN)
//Windows Firewall Test
bool __fastcall FirewallTest(const uint16_t Protocol)
{
//Initialization
	std::shared_ptr<sockaddr_storage> SockAddr(new sockaddr_storage());
	memset(SockAddr.get(), 0, sizeof(sockaddr_storage));
	SYSTEM_SOCKET FirewallSocket = 0;

//Ramdom number distribution initialization
	std::uniform_int_distribution<int> RamdomDistribution(DYNAMIC_MIN_PORT, UINT16_MAX - 1U);

//IPv6
	if (Protocol == AF_INET6)
	{
		((PSOCKADDR_IN6)SockAddr.get())->sin6_addr = in6addr_any;
		((PSOCKADDR_IN6)SockAddr.get())->sin6_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));
		SockAddr->ss_family = AF_INET6;
		FirewallSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

	//Bind local socket.
		if (FirewallSocket == INVALID_SOCKET)
		{
			return false;
		}
		else if (bind(FirewallSocket, (PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in6)) == SOCKET_ERROR)
		{
			((PSOCKADDR_IN6)SockAddr.get())->sin6_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));
			size_t Index = 0;
			while (bind(FirewallSocket, (PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in6)) == SOCKET_ERROR)
			{
				if (Index < LOOP_MAX_TIMES && WSAGetLastError() == WSAEADDRINUSE)
				{
					((PSOCKADDR_IN6)SockAddr.get())->sin6_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));

					++Index;
					continue;
				}
				else {
					closesocket(FirewallSocket);
					return false;
				}
			}
		}
	}
//IPv4
	else {
		((PSOCKADDR_IN)SockAddr.get())->sin_addr.s_addr = INADDR_ANY;
		((PSOCKADDR_IN)SockAddr.get())->sin_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));
		SockAddr->ss_family = AF_INET;
		FirewallSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	//Bind local socket.
		if (FirewallSocket == INVALID_SOCKET)
		{
			return false;
		}
		else if (bind(FirewallSocket, (PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in)) == SOCKET_ERROR)
		{
			((PSOCKADDR_IN)SockAddr.get())->sin_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));
			size_t Index = 0;
			while (bind(FirewallSocket, (PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in)) == SOCKET_ERROR)
			{
				if (Index < LOOP_MAX_TIMES && WSAGetLastError() == WSAEADDRINUSE)
				{
					((PSOCKADDR_IN)SockAddr.get())->sin_port = htons((uint16_t)RamdomDistribution(*Parameter.RamdomEngine));

					++Index;
					continue;
				}
				else {
					closesocket(FirewallSocket);
					return false;
				}
			}
		}
	}

	closesocket(FirewallSocket);
	return true;
}
#endif
