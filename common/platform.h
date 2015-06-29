#ifndef EQEMU_PLATFORM_H
#define EQEMU_PLATFORM_H

enum EQEmuExePlatform
{
	ExePlatformNone = 0,
	ExePlatformZone,
	ExePlatformWorld,
	ExePlatformLogin,
	ExePlatformQueryServ,
	ExePlatformWebInterface,
	ExePlatformUCS,
	ExePlatformLaunch,
	ExePlatformSharedMemory,
	ExePlatformClientImport,
	ExePlatformClientExport
};

void RegisterExecutablePlatform(EQEmuExePlatform p);
const EQEmuExePlatform& GetExecutablePlatform();
int GetExecutablePlatformInt();

#endif
