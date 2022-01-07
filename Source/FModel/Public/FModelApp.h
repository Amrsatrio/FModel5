#pragma once

#include "CoreMinimal.h"
#include "ISlateReflectorModule.h"
#include "FModel.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoDispatcher.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "Paks.h"
#include "Widgets/Docking/SDockTab.h"

int RunApplication(const TCHAR* Commandline);

enum class EVfsType
{
	Pak,
	IoStore
};

struct FVfs
{
	TRefCountPtr<FPakFile> PakFile;
	FIoStoreReader* IoStoreReader;
	EVfsType Type;
	FString Path;

	FVfs(TRefCountPtr<FPakFile> InPakFile)
		: PakFile(InPakFile)
		, Type(EVfsType::Pak)
		, Path(InPakFile->GetFilename())
	{}

	FVfs(FIoStoreReader* InIoStoreReader, const FString& InPath)
		: IoStoreReader(InIoStoreReader)
		, Type(EVfsType::IoStore)
		, Path(InPath)
	{}

	FString GetName() const { return FPaths::GetCleanFilename(Path); }

	int64 GetSize() const { return Type == EVfsType::Pak ? PakFile->TotalSize() : 0; }

	FGuid GetEncryptionKeyGuid() const
	{
		switch (Type)
		{
		case EVfsType::Pak:
			return PakFile->GetInfo().EncryptionKeyGuid;
		case EVfsType::IoStore:
			return IoStoreReader->GetEncryptionKeyGuid();
		default:
			check(false);
			return FGuid();
		}
	}

	bool IsEncrypted() const
	{
		switch (Type)
		{
		case EVfsType::Pak:
			return !!PakFile->GetInfo().bEncryptedIndex;
		case EVfsType::IoStore:
			return !!(IoStoreReader->GetContainerFlags() & EIoContainerFlags::Encrypted);
		default:
			check(false);
			return false;
		}
	}

	FString GetMountPoint() const
	{
		FString MountPoint = TEXT("");
		switch (Type)
		{
		case EVfsType::Pak:
			MountPoint = PakFile->GetMountPoint();
			break;
		default:
			check(false);
		}
		return MountPoint;
	}

	IFileHandle* OpenRead(IPlatformFile* LowerLevel, const FString& Filename) const
	{
		FPakEntry Entry;
		if (PakFile->Find(Filename, &Entry) == FPakFile::EFindResult::Found)
		{
			return FPakUtils::CreatePakFileHandle(LowerLevel, PakFile, &Entry);
		}
		return nullptr;
	}

	// Don't care, just use path for comparison
	friend uint32 GetTypeHash(const FVfs& Vfs) { return GetTypeHash(Vfs.Path); }
	friend bool operator==(const FVfs& Lhs, const FVfs& Rhs) { return Lhs.Path == Rhs.Path; }
};

// CUE4Parse & JFortniteParse equivalent: DefaultFileProvider
class FVfsPlatformFile// : public IPlatformFile
{
public:
	TSet<FVfs> UnloadedVfs;
	TSet<FVfs> MountedVfs;
	TMap<FGuid, FAES::FAESKey> Keys;
	TSet<FGuid> RequiredKeys;
	FCriticalSection CollectionsLock;

	FString Directory;
	IPlatformFile* LowerLevel;

	FVfsPlatformFile(const FString& InDirectory) : Directory(InDirectory)
	{
		FPakPlatformFile::GetPakCustomEncryptionDelegate().BindLambda([this](uint8* InData, uint32 InDataSize, FGuid InEncryptionKeyGuid)
		{
			FAES::FAESKey& Key = Keys.FindChecked(InEncryptionKeyGuid);
			FAES::DecryptData(InData, InDataSize, Key);
		});
	}

	virtual ~FVfsPlatformFile() = default;

	// Specific to DefaultFileProvider aka local file system
	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CmdLine) /*override*/
	{
		LowerLevel = Inner;
		Inner->IterateDirectory(*Directory, [this](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
		{
			FString Extension = FPaths::GetExtension(FilenameOrDirectory);
			if (Extension == TEXT("pak"))
			{
				UE_LOG(LogFModel, Display, TEXT("Found pak file %s"), FilenameOrDirectory);
				FPakFile* PakFile = new FPakFile(LowerLevel, FilenameOrDirectory, false, false);
				if (PakFile->IsValid())
				{
					FScopeLock Lock(&CollectionsLock);
					if (PakFile->GetInfo().bEncryptedIndex && !RequiredKeys.Contains(PakFile->GetInfo().EncryptionKeyGuid))
					{
						RequiredKeys.Add(PakFile->GetInfo().EncryptionKeyGuid);
					}
					UnloadedVfs.Emplace(PakFile);
				}
			}
			else if (Extension == TEXT("utoc"))
			{
				// @todo: Mount IoStores
			}
			return true;
		});
		return true;
	}

	int32 Mount()
	{
		TArray<FVfs> VfsToMount;
		{
			FScopeLock Lock(&CollectionsLock);
			for (const FVfs& Vfs : UnloadedVfs)
			{
				if (!Vfs.IsEncrypted())
				{
					VfsToMount.Add(Vfs);
				}
			}
		}
		TAtomic<int32> CountNewMounts(0);
		ParallelFor(VfsToMount.Num(), [&](int32 Index)
		{
			FVfs& Vfs = VfsToMount[Index];
			Vfs.PakFile = new FPakFile(LowerLevel, *Vfs.PakFile->GetFilename(), false, true /*load index this time*/);
			check(Vfs.PakFile->IsValid());
			FString MountPoint = Vfs.PakFile->GetMountPoint();
			NormalizeMountPoint(MountPoint);
			Vfs.PakFile->SetMountPoint(*MountPoint);
			{
				FScopeLock Lock(&CollectionsLock);
				// @todo: Merge files
				UnloadedVfs.Remove(Vfs);
				MountedVfs.Add(Vfs);
			}
			++CountNewMounts;
		});
		return CountNewMounts;
	}

	int32 SubmitKey(const FGuid& EncryptionKeyGuid, const FAES::FAESKey& Key)
	{
		TMap<FGuid, FAES::FAESKey> SingletonMap;
		SingletonMap.Add(EncryptionKeyGuid, Key);
		return SubmitKeys(SingletonMap);
	}

	int32 SubmitKeys(TMap<FGuid, FAES::FAESKey>& InKeys)
	{
		TArray<FVfs> VfsToMount;
		{
			FScopeLock Lock(&CollectionsLock);
			for (auto Pair : InKeys)
			{
				for (const FVfs& Vfs : UnloadedVfs)
				{
					if (Vfs.GetEncryptionKeyGuid() == Pair.Key)
					{
						Keys.Add(Pair.Key, Pair.Value); // @todo: Invalid key handling
						VfsToMount.Add(Vfs);
					}
				}
			}
		}
		TAtomic<int32> CountNewMounts(0);
		ParallelFor(VfsToMount.Num(), [&](int32 Index)
		{
			FVfs& Vfs = VfsToMount[Index];
			Vfs.PakFile = new FPakFile(LowerLevel, *Vfs.PakFile->GetFilename(), false, true /*load index this time*/);
			check(Vfs.PakFile->IsValid());
			FString MountPoint = Vfs.PakFile->GetMountPoint();
			NormalizeMountPoint(MountPoint);
			Vfs.PakFile->SetMountPoint(*MountPoint);
			{
				FScopeLock Lock(&CollectionsLock);
				// @todo: Merge files
				UnloadedVfs.Remove(Vfs);
				MountedVfs.Add(Vfs);
			}
			++CountNewMounts;
		});
		return CountNewMounts;
	}

	IFileHandle* Read(const FString& Path)
	{
		FScopeLock Lock(&CollectionsLock);
		for (const FVfs& Vfs : MountedVfs)
		{
			if (IFileHandle* Handle = Vfs.OpenRead(LowerLevel, Path))
			{
				return Handle;
			}
		}
		return nullptr;
	}

	virtual IPlatformFile* GetLowerLevel() /*override*/ { return LowerLevel; }
	virtual void SetLowerLevel(IPlatformFile* NewLowerLevel) /*override*/ { LowerLevel = NewLowerLevel; }

private:
	static void NormalizeMountPoint(FString& MountPoint)
	{
		if (MountPoint.StartsWith(TEXT("../../../")))
		{
			MountPoint = MountPoint.Mid(9);
		}
	}
};

class FFModelApp
{
public:
	static FFModelApp& Get()
	{
		static FFModelApp* Instance = nullptr;
		if (!Instance)
		{
			Instance = new FFModelApp();
		}
		return *Instance;
	}

	FVfsPlatformFile* Provider;

	FFModelApp()
	{
		Provider = new FVfsPlatformFile("C:\\Program Files\\Epic Games\\Fortnite\\FortniteGame\\Content\\Paks");
		// Provider = new FVfsPlatformFile("D:\\Downloads\\TestPak");
		IPlatformFile* LowerLevelPlatformFile = &FPlatformFileManager::Get().GetPlatformFile();
		Provider->Initialize(LowerLevelPlatformFile, nullptr);
	}
};
