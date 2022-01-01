#pragma once

#include "FModelApp.h"
#include "SKeychainWindow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "Widgets/SWindow.h"

struct FVfsEntry
{
	FString Name;
	int64 Size;
	FGuid EncryptionKeyGuid;
	int32 ChunkId;
	FPakFile* PakFile;

	explicit FVfsEntry(const FVfs& Vfs)
		: Name(Vfs.GetName())
		, Size(Vfs.GetSize())
		, EncryptionKeyGuid(Vfs.GetEncryptionKeyGuid())
		, ChunkId(FPlatformMisc::GetPakchunkIndexFromPakFile(Name))
		, PakFile(Vfs.PakFile /*paks only for now*/) { }
};

struct FFileTreeNode
{
	TMap<FString, TSharedPtr<FFileTreeNode>> Entries;
	TArray<TSharedPtr<FFileTreeNode>>* EntriesList = nullptr;
	FString Path;

	void AddEntry(const FString& sEntry, int wBegIndex = 0);

	TArray<TSharedPtr<FFileTreeNode>>* GetEntries();

	bool IsFile() const { return Entries.Num() == 0; }

	FString GetName() const { return FPaths::GetCleanFilename(Path); }
};

class SMainWindow : public SWindow
{
protected:
	TSharedPtr<class FTabManager> TabManager;
	TArray<TSharedPtr<FString>> Options;
	TArray<TSharedPtr<FVfsEntry>> Archives;
	FFileTreeNode Files;

public:
	SLATE_BEGIN_ARGS(SMainWindow) { }

	SLATE_END_ARGS()

	// SMainWindow();
	// virtual	~SMainWindow();

	/** Widget constructor */
	void Construct(const FArguments& Args);

	void MakeDirectoryMenu(FMenuBuilder& MenuBuilder);
	void MakeAssetsMenu(FMenuBuilder& MenuBuilder);
	void MakeToolsMenu(FMenuBuilder& MenuBuilder);
	void MakeHelpMenu(FMenuBuilder& MenuBuilder);

	void BuildArchivesList();
	void BuildFilesList();
};
