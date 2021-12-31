#include "FModelApp.h"
#include "Windows/WindowsHWrapper.h"

/**
 * WinMain, called when the application is started
 */
int WINAPI WinMain( _In_ HINSTANCE hInInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int nCmdShow )
{
	RunApplication(GetCommandLineW());
	return 0;
}
