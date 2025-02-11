//----------------------------------------------------------------------
// This file is part of VCC (Virtual Color Computer).
// Vcc is Copyright 2015 by Joseph Forgione
//
// VCC (Virtual Color Computer) is free software, you can redistribute it 
// and/or modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
//
// VCC (Virtual Color Computer) is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
// Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with VCC (Virtual Color Computer).  If not, see 
// <http://www.gnu.org/licenses/>.
//----------------------------------------------------------------------


//#include <windows.h>
//#include <windowsx.h>
//#include <stdio.h>
//#include <conio.h>
//#include <dinput.h>
//#include "resource.h"


#include <windows.h>
#include <windowsx.h>
//#include <stdio.h>
//#include <iostream>
#include "defines.h"
#include "resource.h"
#include "sdcavr.h"
#include "..\fileops.h"
#include "..\logger.h"

static char IniFile[MAX_PATH]={0};    // Ini file name from config
static char SDCardPath[MAX_PATH]={0}; // Path to SD card contents
static HINSTANCE hinstDLL;            // DLL handle
static HWND hConfDlg = NULL;          // Config dialog

typedef void (*ASSERTINTERUPT) (unsigned char,unsigned char);
typedef void (*DYNAMICMENUCALLBACK)( char *,int, int);

static void (*AssertInt)(unsigned char,unsigned char)=NULL;
static void (*DynamicMenuCallback)( char *,int, int)=NULL;

LRESULT CALLBACK ConfigSDC(HWND, UINT, WPARAM, LPARAM );

void LoadConfig(void);
void SaveConfig(void);
void BuildDynaMenu(void);
void SetSDCardPath(void);
void LoadExtRom(char *);

using namespace std;

//------------------------------------------------------------
// DLL entry point 
//------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID rsvd)
{
    if (reason == DLL_PROCESS_ATTACH) {
        hinstDLL = hinst;
        LoadExtRom("SDC.ROM");
		SDCInit();

    } else if (reason == DLL_PROCESS_DETACH) {
        if (hConfDlg) { 
			DestroyWindow(hConfDlg);
			hConfDlg = NULL;
		}
        hinstDLL = NULL;
        SDCReset();
    }
    return TRUE;
}

//------------------------------------------------------------
// Register the DLL and build menu
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) void
    ModuleName(char *ModName,char *CatNumber,DYNAMICMENUCALLBACK Temp)
    {
        LoadString(hinstDLL, IDS_MODULE_NAME, ModName, MAX_LOADSTRING);
        LoadString(hinstDLL, IDS_CATNUMBER, CatNumber, MAX_LOADSTRING);
        DynamicMenuCallback = Temp;
        if (DynamicMenuCallback != NULL) BuildDynaMenu();
        return ;
    }
}

//------------------------------------------------------------
// Write to port
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) void
    PackPortWrite(unsigned char Port,unsigned char Data)
    {
        SDCWrite(Data,Port);
        return;
    }
}

//------------------------------------------------------------
// Read from port
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) unsigned char PackPortRead(unsigned char Port)
    {
        return(SDCRead(Port));
    }
}

//------------------------------------------------------------
// Reset module
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) unsigned char
    ModuleReset(void)
    {        //SDCReset();
        return 0;
    }
}

//-----------------------------------------------------------------------
//  Dll export run config dialog
//-----------------------------------------------------------------------
extern "C"
{
	__declspec(dllexport) void ModuleConfig(unsigned char MenuID)
	{
		if (hConfDlg == NULL) {
    		CreateDialog (hinstDLL, (LPCTSTR) IDD_CONFIG,
					       GetActiveWindow(),(DLGPROC)ConfigSDC);
		}
    	ShowWindow(hConfDlg,1);
		SetFocus(hConfDlg);
    	return;
	}
}

//------------------------------------------------------------
// Configure the SDC
//------------------------------------------------------------
LRESULT CALLBACK
ConfigSDC(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {

    case WM_CLOSE:
        EndDialog(hDlg,LOWORD(wParam));

	case WM_INITDIALOG:
        hConfDlg=hDlg;
		break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

		case IDOK:
            EndDialog(hDlg,LOWORD(wParam));
			break;

		case IDCANCEL:
            EndDialog(hDlg,LOWORD(wParam));
			break;

		default:
        	break;
        }
    }
    return (INT_PTR) 0;
}

//------------------------------------------------------------
// Capture the Fuction transfer point for the CPU assert interupt
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport)
    void AssertInterupt(ASSERTINTERUPT Dummy)
    {
        AssertInt=Dummy;
        return;
    }
}
//------------------------------------------------------------
//  Heart beat
//------------------------------------------------------------
/*
extern "C"
{
    __declspec(dllexport) void HeartBeat(void)
    {
        return;
    }
}
*/

//------------------------------------------------------------
// Return SDC status.
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) void
    ModuleStatus(char *MyStatus)
    {
        MyStatus = "SDC";
        return ;
    }
}

//------------------------------------------------------------
// Set ini file path and load HD config settings
//------------------------------------------------------------
extern "C"
{
    __declspec(dllexport) void
    SetIniPath (char *IniFilePath)
    {
        strcpy(IniFile,IniFilePath);
        LoadConfig();
        return;
    }
}

/*
void CPUAssertInterupt(unsigned char Interupt,unsigned char Latencey)
{
    AssertInt(Interupt,Latencey);
    return;
}
*/

//------------------------------------------------------------
// Get SDC pathname from user
//------------------------------------------------------------
void SetSDCardPath(void)
{
	// Prompt user for path
    return;
}

//------------------------------------------------------------
// Get configuration items from ini file
//------------------------------------------------------------
void LoadConfig(void)
{
    GetPrivateProfileString("SDC", "SDCardPath", "",
                             SDCardPath, MAX_PATH, IniFile);
    // Create config menu
    BuildDynaMenu();
    return;
}

//------------------------------------------------------------
// Save config saves the hard disk path and vhd file names
//------------------------------------------------------------
void SaveConfig(void)
{
    WritePrivateProfileString("SDC", "SDCardPath",SDCardPath,IniFile);
    return;
}

//-------------------------------------------------------------
//  Build Cartridge menu
//-------------------------------------------------------------
void BuildDynaMenu(void)
{
    DynamicMenuCallback("",0,0);
    DynamicMenuCallback("",6000,0);
    DynamicMenuCallback("SDC Config",5016,2);
    DynamicMenuCallback("",1,0);
}

//-------------------------------------------------------------
// Load SDC rom
//-------------------------------------------------------------
void LoadExtRom(char * path) {
}

