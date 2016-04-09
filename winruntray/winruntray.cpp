// winruntray.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "winruntray.h"
#include "resource.h"

const UINT WM_TRAY = WM_USER + 1;
HINSTANCE g_hInstance = NULL;
HICON g_hIcon = NULL;
DWORD g_dwThreadId = NULL;
HANDLE g_hThread = NULL;

const TCHAR g_szRegKeyName[] = _T("SOFTWARE\\Misc\\WinRunTray");
const TCHAR g_szDirectoryToMonitor[] = _T("Watch");

typedef void (CALLBACK *FileChangeCallback)(HWND, LPTSTR, DWORD, LPARAM);

typedef struct tagDIR_MONITOR
{
	OVERLAPPED ol;
	HANDLE     hDir;
	HWND       hWnd;
	LPCTSTR    lpDirectory;
	BYTE       buffer[32 * 1024];
	LPARAM     lParam;
	DWORD      notifyFilter;
	BOOL       fStop;
	FileChangeCallback callback;
} *HDIR_MONITOR;

HDIR_MONITOR g_hMonitor;

void LoadStringSafe(UINT nStrID, LPTSTR szBuf, UINT nBufLen)
{
	UINT nLen = LoadString(g_hInstance, nStrID, szBuf, nBufLen);
	if (nLen >= nBufLen)
		nLen = nBufLen - 1;
	szBuf[nLen] = 0;
}

void ExecuteCommand(LPTSTR szCmdLine)
{
	TCHAR szTxt[0x100];
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	BOOL bResult;

	wsprintf(szTxt, _T("INFO: In ExecuteCommand('%s').\n"), szCmdLine);
	OutputDebugString(szTxt);

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	bResult = CreateProcess(
		NULL,           // No module name (use command line)
		szCmdLine,      // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi);           // Pointer to PROCESS_INFORMATION structure

	if (bResult == FALSE)
	{
		wsprintf(szTxt, _T("ERROR: CreateProcess() failed. (Error: %d)\n"), GetLastError());
		OutputDebugString(szTxt);
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

int ExtractCommandLine(LPCTSTR szFile, LPTSTR szCmdLine)
{
	TCHAR szTxt[0x100];
	HANDLE hFile;
	DWORD dwBytesRead;
	DWORD dwSize;
	BOOL bResult;
	TCHAR szBuffer[MAX_PATH];

	wsprintf(szTxt, _T("INFO: In ExtractCommandLine().\n"));
	OutputDebugString(szTxt);

	hFile = CreateFile(
		szFile,                // file to open
		GENERIC_READ,          // open for reading
		FILE_SHARE_READ,       // share for reading
		NULL,                  // default security
		OPEN_EXISTING,         // existing file only
		FILE_ATTRIBUTE_NORMAL, // normal file
		NULL);                 // no attr. template
			    
	if (hFile == INVALID_HANDLE_VALUE)
	{
		wsprintf(szTxt, _T("ERROR: Unable to open file \"%s\" for read. (Error: %d)\n"), szFile, GetLastError());
		OutputDebugString(szTxt);
		return -1;
	}

	if ((dwSize = GetFileSize(hFile, 0)) == 0)
	{
		wsprintf(szTxt, _T("ERROR: File \"%s\" is of zero length.\n"), szFile);
		OutputDebugString(szTxt);
		return -1;
	}

	bResult = ReadFile(hFile, szBuffer, dwSize, &dwBytesRead, NULL);

	if (bResult == FALSE)
	{
		wsprintf(szTxt, _T("ERROR: Unable to read file \"%s\". (Error: %d)\n"), szFile, GetLastError());
		OutputDebugString(szTxt);
		return -1;
	}

	szBuffer[dwBytesRead/sizeof(TCHAR)] =  0;
	wsprintf(szCmdLine, _T("%S"), szBuffer);
	//szCmdLine[dwBytesRead] = 0;
	//OutputDebugString(szCmdLine);

	CloseHandle(hFile);

	return 0;
}

void CALLBACK FileCallback(HWND hWnd, LPTSTR szFile, DWORD action, LPARAM lParam)
{
	TCHAR szTxt[0x100];
	TCHAR szCmdLine[MAX_PATH];
	
	switch (action)
	{
	case FILE_ACTION_ADDED:
		wsprintf(szTxt, _T("INFO: FILE_ACTION_ADDED: '%s'\n"), szFile);

		Sleep(1000);
		ExtractCommandLine(szFile, (LPTSTR) &szCmdLine);
		ExecuteCommand(szCmdLine);
		break;

	case FILE_ACTION_REMOVED:
		wsprintf(szTxt, _T("INFO: FILE_ACTION_REMOVED: '%s'\n"), szFile);
		break;

	case FILE_ACTION_MODIFIED:
		wsprintf(szTxt, _T("INFO: FILE_ACTION_MODIFIED: '%s'\n"), szFile);
		break;

	default:
		wsprintf(szTxt, _T("INFO: Ignored change action of type: %d\n"), action);
		break;
	}
	
	OutputDebugString(szTxt);
}

BOOL RefreshMonitoring(HDIR_MONITOR pMonitor);

VOID CALLBACK MonitorCallback(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	TCHAR                    szTxt[0x100];
	TCHAR                    szFile[MAX_PATH];
	PFILE_NOTIFY_INFORMATION pNotify;
	HDIR_MONITOR             pMonitor = (HDIR_MONITOR) lpOverlapped;
	size_t                   offset = 0;
	
	wsprintf(szTxt, _T("INFO: In MonitorCallback().\n"));
	OutputDebugString(szTxt);

	if (dwErrorCode == ERROR_SUCCESS)
	{
		do
		{
			pNotify = (PFILE_NOTIFY_INFORMATION) &pMonitor->buffer[offset];
			offset += pNotify->NextEntryOffset;

			wsprintf(szFile, _T("%s\\%s"), pMonitor->lpDirectory, pNotify->FileName);

			pMonitor->callback(pMonitor->hWnd, szFile, pNotify->Action, pMonitor->lParam);

		} while (pNotify->NextEntryOffset != 0);
	}

	if (!pMonitor->fStop)
	{
		RefreshMonitoring(pMonitor);
	}
}

// Refreshes the directory monitoring.

BOOL RefreshMonitoring(HDIR_MONITOR pMonitor)
{
	TCHAR szTxt[0x100];
	
	wsprintf(szTxt, _T("INFO: In RefreshMonitoring().\n"));
	OutputDebugString(szTxt);

	return ReadDirectoryChangesW(pMonitor->hDir, pMonitor->buffer, sizeof(pMonitor->buffer), 
		FALSE, pMonitor->notifyFilter, NULL, &pMonitor->ol, MonitorCallback);
}

// Stops monitoring a directory.

void StopMonitoring(HDIR_MONITOR pMonitor)
{
	TCHAR szTxt[0x100];
	
	wsprintf(szTxt, _T("INFO: In StopMonitoring()\n"));
	OutputDebugString(szTxt);

	if (pMonitor)
	{
		pMonitor->fStop = TRUE;

		CancelIo(pMonitor->hDir);

		if (!HasOverlappedIoCompleted(&pMonitor->ol))
		{
			SleepEx(5, TRUE);
		}

		CloseHandle(pMonitor->ol.hEvent);
		CloseHandle(pMonitor->hDir);
		HeapFree(GetProcessHeap(), 0, pMonitor);
	}
}


// Starts monitoring a directory.

HDIR_MONITOR StartMonitoring(HWND hWnd, LPCTSTR szDirectory, DWORD notifyFilter, FileChangeCallback callback)
{
	TCHAR szTxt[0x100];
	HDIR_MONITOR pMonitor;
	
	wsprintf(szTxt, _T("INFO: StartMonitoring('%s').\n"), szDirectory);
	OutputDebugString(szTxt);

	pMonitor = (HDIR_MONITOR) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pMonitor));

	pMonitor->hDir = CreateFile(
		szDirectory, 
		FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, 
		OPEN_EXISTING, 
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);
	pMonitor->hWnd = hWnd;
	pMonitor->lpDirectory = szDirectory;

	if (pMonitor->hDir != INVALID_HANDLE_VALUE)
	{
		pMonitor->ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		pMonitor->notifyFilter = notifyFilter;
		pMonitor->callback = callback;

		if (RefreshMonitoring(pMonitor))
		{
			return pMonitor;
		}
		else
		{
			wsprintf(szTxt, _T("ERROR: RefreshMonitoring() failed. (Error: %d)\n"), GetLastError());
			OutputDebugString(szTxt);

			CloseHandle(pMonitor->ol.hEvent);
			CloseHandle(pMonitor->hDir);
		}
	}

	wsprintf(szTxt, _T("ERROR: RefreshMonitoring(): CreateFile() failed. (Error: %d)\n"), GetLastError());
	OutputDebugString(szTxt);

	HeapFree(GetProcessHeap(), 0, pMonitor);
	return NULL;
}

LRESULT CALLBACK HiddenWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		NOTIFYICONDATA stData;
		ZeroMemory(&stData, sizeof(stData));
		stData.cbSize = sizeof(stData);
		stData.hWnd = hWnd;
		stData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		stData.uCallbackMessage = WM_TRAY;
		stData.hIcon = g_hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_SMALL));
		LoadStringSafe(IDS_TIP, stData.szTip, _countof(stData.szTip));
		if (!Shell_NotifyIcon(NIM_ADD, &stData))
			return -1; // oops

		g_hMonitor = StartMonitoring(hWnd, _T("C:\\winrun"), 
			FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME, FileCallback);
	}
	return 0;

	case WM_DESTROY:
	{
		NOTIFYICONDATA stData;
		ZeroMemory(&stData, sizeof(stData));
		stData.cbSize = sizeof(stData);
		stData.hWnd = hWnd;
		Shell_NotifyIcon(NIM_DELETE, &stData);
	}
	return 0;

	case WM_TRAY:
		switch (lParam)
		{
		case WM_RBUTTONDOWN:
		{
			HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDC_WINRUNTRAY));
			if (hMenu)
			{
				HMENU hSubMenu = GetSubMenu(hMenu, 0);
				if (hSubMenu)
				{
					POINT stPoint;
					GetCursorPos(&stPoint);

					TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, 
						stPoint.x, stPoint.y, 0, hWnd, NULL);
				}

				DestroyMenu(hMenu);
			}
		}
		break;
		}
		return 0;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDM_EXIT:
			StopMonitoring(g_hMonitor);
			PostQuitMessage(0);
			return 0;
		}
		break;

	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow)
{
	WNDCLASS stWC;
	ZeroMemory(&stWC, sizeof(stWC));
	stWC.lpszClassName = _T("WinRunTray-8139f68b-4b78-47cd-b9db-a27788551c69");

	HWND hHiddenWnd = FindWindow(stWC.lpszClassName, NULL);
	if (hHiddenWnd)
		PostMessage(hHiddenWnd, WM_TRAY, 0, WM_LBUTTONDBLCLK);
	else
	{
		stWC.hInstance = hInstance;
		stWC.lpfnWndProc = HiddenWndProc;

		ATOM aClass = RegisterClass(&stWC);
		if (aClass)
		{
			g_hInstance = hInstance;
			if (hHiddenWnd = CreateWindow((LPCTSTR) aClass, _T(""), 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL))
			{
				BOOL done = FALSE;
				MSG stMsg;
				/*
				while (GetMessage(&stMsg, NULL, 0, 0) > 0)
				{
					TranslateMessage(&stMsg);
					DispatchMessage(&stMsg);
				}
				*/
				while (!done)
				{
					/* Wait for either an APC or a message. */
					while (WAIT_IO_COMPLETION ==
						MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE)); // Do nothing

					/* One or more messages have arrived. */
					while (PeekMessage(&stMsg, NULL, 0, 0, PM_REMOVE))
					{
						if (stMsg.message == WM_QUIT)
						{
							done = TRUE;
							break;
						}

						TranslateMessage(&stMsg);
						DispatchMessage(&stMsg);
					}
				}
				if (IsWindow(hHiddenWnd))
					DestroyWindow(hHiddenWnd);
			}
			UnregisterClass((LPCTSTR)aClass, g_hInstance);
		}
	}
	return 0;
}

