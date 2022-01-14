#pragma once

#include "CoreMinimal.h"
#include "ISlateReflectorModule.h"
#include "FModel.h"
#include "Async/ParallelFor.h"
#include "FilePackageStore.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoContainerHeader.h"
#include "IoDispatcherFileBackend.h"
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
	TSharedPtr<FIoStoreTocResource> IoStoreToc;
	EVfsType Type;
	FString Path;
	int64 Size;

	FVfs(TRefCountPtr<FPakFile> InPakFile)
		: PakFile(InPakFile)
		, Type(EVfsType::Pak)
		, Path(InPakFile->GetFilename())
	{
		Size = PakFile->TotalSize();
	}

	FVfs(TSharedPtr<FIoStoreTocResource> InIoStoreToc, const FString& InPath)
		: IoStoreToc(InIoStoreToc)
		, Type(EVfsType::IoStore)
		, Path(InPath)
	{
		FString ContainerPath = InPath.LeftChop(5) + TEXT(".ucas");
		Size = IFileManager::Get().FileSize(*ContainerPath); // Don't care about partitions yet
	}

	FString GetName() const { return FPaths::GetCleanFilename(Path); }

	FGuid GetEncryptionKeyGuid() const
	{
		switch (Type)
		{
		case EVfsType::Pak:
			return PakFile->GetInfo().EncryptionKeyGuid;
		case EVfsType::IoStore:
			return IoStoreToc->Header.EncryptionKeyGuid;
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
			return EnumHasAnyFlags(IoStoreToc->Header.ContainerFlags, EIoContainerFlags::Encrypted);
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
class FVfsPlatformFile : public IPlatformFile
{
public:
	TSet<FVfs> UnloadedVfs;
	TSet<FVfs> MountedVfs;
	TMap<FGuid, FAES::FAESKey> Keys;
	TSet<FGuid> RequiredKeys;
	FCriticalSection CollectionsLock;

	TArray<FString> Directories;
	IPlatformFile* LowerLevel;

	TSharedPtr<FFileIoStore> IoDispatcherFileBackend;
	TSharedPtr<FFilePackageStore> FilePackageStore;

	FVfsPlatformFile(const FString& InDirectory)
	{
		Directories.Add(InDirectory);
		FPakPlatformFile::GetPakCustomEncryptionDelegate().BindLambda([this](uint8* InData, uint32 InDataSize, FGuid InEncryptionKeyGuid)
		{
			FAES::FAESKey& Key = Keys.FindChecked(InEncryptionKeyGuid);
			FAES::DecryptData(InData, InDataSize, Key);
		});
	}

	virtual ~FVfsPlatformFile() override = default;

	// Specific to DefaultFileProvider aka local file system
	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CmdLine) override
	{
		LowerLevel = Inner;
		for (const FString& Directory : Directories)
		{
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
						if (PakFile->GetInfo().bEncryptedIndex)
						{
							RequiredKeys.Add(PakFile->GetInfo().EncryptionKeyGuid);
						}
						UnloadedVfs.Emplace(PakFile);
					}
				}
				else if (Extension == TEXT("utoc"))
				{
					UE_LOG(LogFModel, Display, TEXT("Found IoStore %s"), FilenameOrDirectory);
					if (!IoDispatcherFileBackend.IsValid())
					{
						FIoDispatcher& IoDispatcher = FIoDispatcher::Get();
						IoDispatcherFileBackend = CreateIoDispatcherFileBackend();
						IoDispatcher.Mount(IoDispatcherFileBackend.ToSharedRef());
						FilePackageStore = MakeShared<FFilePackageStore>();
					}
					TSharedPtr<FIoStoreTocResource> Toc = MakeShared<FIoStoreTocResource>();
					if (FIoStoreTocResource::Read(FilenameOrDirectory, EIoStoreTocReadOptions::ReadDirectoryIndex, *Toc).IsOk())
					{
						FScopeLock Lock(&CollectionsLock);
						if (EnumHasAnyFlags(Toc->Header.ContainerFlags, EIoContainerFlags::Encrypted))
						{
							RequiredKeys.Add(Toc->Header.EncryptionKeyGuid);
						}
						UnloadedVfs.Add(FVfs(Toc, FilenameOrDirectory));
					}
				}
				return true;
			});
		}
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
			if (Vfs.Type == EVfsType::Pak)
			{
				Vfs.PakFile = new FPakFile(LowerLevel, *Vfs.PakFile->GetFilename(), false, true /*load index this time*/);
				check(Vfs.PakFile->IsValid());
				FString MountPoint = Vfs.PakFile->GetMountPoint();
				NormalizeMountPoint(MountPoint);
				Vfs.PakFile->SetMountPoint(*MountPoint);
			}
			else
			{
				IoDispatcherFileBackend->Mount(*Vfs.Path, 0, FGuid(), FAES::FAESKey());
			}
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
			if (Vfs.Type == EVfsType::Pak)
			{
				Vfs.PakFile = new FPakFile(LowerLevel, *Vfs.PakFile->GetFilename(), false, true /*load index this time*/);
				check(Vfs.PakFile->IsValid());
				FString MountPoint = Vfs.PakFile->GetMountPoint();
				NormalizeMountPoint(MountPoint);
				Vfs.PakFile->SetMountPoint(*MountPoint);
			}
			else
			{
				FGuid EncryptionKeyGuid = Vfs.GetEncryptionKeyGuid();
				IoDispatcherFileBackend->Mount(*Vfs.Path, 0, EncryptionKeyGuid, Keys.FindChecked(EncryptionKeyGuid));
			}
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
	virtual const TCHAR* GetName() const override { return TEXT("Custom"); }
	virtual bool FileExists(const TCHAR* Filename) override { return false; }
	virtual int64 FileSize(const TCHAR* Filename) override { return -1; }
	virtual bool DeleteFile(const TCHAR* Filename) override { return false; }
	virtual bool IsReadOnly(const TCHAR* Filename) override { return false; }
	virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override { return false; }
	virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override { return false; }
	virtual FDateTime GetTimeStamp(const TCHAR* Filename) override { return FDateTime::MinValue(); }
	virtual void SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override {}
	virtual FDateTime GetAccessTimeStamp(const TCHAR* Filename) override { return FDateTime::MinValue(); }
	virtual FString GetFilenameOnDisk(const TCHAR* Filename) override { return TEXT(""); }
	virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite) override { return nullptr; }
	virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend, bool bAllowRead) override { return nullptr; }
	virtual bool DirectoryExists(const TCHAR* Directory) override { return false; }
	virtual bool CreateDirectory(const TCHAR* Directory) override { return false; }
	virtual bool DeleteDirectory(const TCHAR* Directory) override { return false; }
	virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) override { return FFileStatData(); }
	virtual bool IterateDirectory(const TCHAR* Directory, FDirectoryVisitor& Visitor) override { return false; }
	virtual bool IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitor& Visitor) override { return false; }

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
