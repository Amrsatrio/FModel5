#pragma once

#include "SKeychainWindow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "Widgets/SWindow.h"

struct FPakListEntry
{
	FPakListEntry()
		: ReadOrder(0)
		, PakFile(nullptr)
	{}

	uint32					ReadOrder;
	TRefCountPtr<FPakFile>	PakFile;

	FORCEINLINE bool operator < (const FPakListEntry& RHS) const
	{
		return ReadOrder > RHS.ReadOrder;
	}
};

struct FPakListDeferredEntry
{
	FString Filename;
	FString Path;
	uint32 ReadOrder;
	FGuid EncryptionKeyGuid;
	int32 PakchunkIndex;
};

struct FVfsEntry
{
	FString Name;
	FGuid EncryptionKeyGuid;
	int32 ChunkId;
	FPakFile* PakFile;

	explicit FVfsEntry(const FPakListEntry& PakListEntry)
		: Name(FPaths::GetBaseFilename(PakListEntry.PakFile->GetFilename()))
		, EncryptionKeyGuid(PakListEntry.PakFile->GetInfo().EncryptionKeyGuid)
		, ChunkId(PakListEntry.PakFile->PakGetPakchunkIndex())
		, PakFile(PakListEntry.PakFile.GetReference()) { }

	explicit FVfsEntry(const FPakListDeferredEntry& PakListDeferredEntry)
		: Name(FPaths::GetBaseFilename(PakListDeferredEntry.Filename))
		, EncryptionKeyGuid(PakListDeferredEntry.EncryptionKeyGuid)
		, ChunkId(PakListDeferredEntry.PakchunkIndex)
		, PakFile(nullptr) { }
};

struct FFileTreeNode;

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
	TArray<TSharedPtr<FString>> Options, Dummy;
	TArray<TSharedPtr<FVfsEntry>> Entries;
	FFileTreeNode Files;
	TSharedPtr<SKeychainWindow> KeychainWindow;

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

	void BuildPakList();
	void BuildFilesList();
};
