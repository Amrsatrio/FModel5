#pragma once

#include "CoreMinimal.h"
#include "ISlateReflectorModule.h"
#include "FModel.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "Widgets/Docking/SDockTab.h"

extern FPakPlatformFile* GPakPlatformFile;

int RunApplication(const TCHAR* Commandline);
