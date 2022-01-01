#pragma once

#include "FModelApp.h"
#include "SKeychainWindow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/STreeView.h"

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

	void Reset()
	{
		Entries.Reset();
		EntriesList = nullptr;
	}
};

enum class ELoadingMode
{
	Single,
	Multiple,
	All,
	AllButNew,
	AllButModified,
	Count
};

inline const TCHAR* LexToString(ELoadingMode Value)
{
	switch (Value)
	{
	case ELoadingMode::Single:			return TEXT("Single");
	case ELoadingMode::Multiple:		return TEXT("Multiple");
	case ELoadingMode::All:				return TEXT("All");
	case ELoadingMode::AllButNew:		return TEXT("All (New)");
	case ELoadingMode::AllButModified:	return TEXT("All (Modified)");
	default:							return TEXT("");
	}
}

class SMainWindow : public SWindow
{
protected:
	TSharedPtr<FTabManager> TabManager;
	TArray<TSharedPtr<ELoadingMode>> LoadingModeOptions;
	TArray<TSharedPtr<FVfsEntry>> Archives;
	FFileTreeNode Files;

	TSharedPtr<SComboBox<TSharedPtr<ELoadingMode>>> ComboBox_LoadingMode;
	TSharedPtr<SListView<TSharedPtr<FVfsEntry>>> List_Archives;
	TSharedPtr<STreeView<TSharedPtr<FFileTreeNode>>> Tree_Files;

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

	void UpdateFilesList();
};
