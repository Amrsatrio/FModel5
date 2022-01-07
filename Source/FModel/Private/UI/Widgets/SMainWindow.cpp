#include "SMainWindow.h"

#include "FModelApp.h"
#include "Brushes/SlateImageBrush.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManagerGeneric.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "Widgets/Views/STreeView.h"

void SMainWindow::MakeDirectoryMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		INVTEXT("Selector"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("AES"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this] { FSlateApplication::Get().AddWindow(SNew(SKeychainWindow)); }))
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Backup"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Archives Info"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
}

void SMainWindow::MakeAssetsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		INVTEXT("Search"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Directories"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddSeparator();
	MenuBuilder.AddMenuEntry(
		INVTEXT("Export Raw Data"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Save Property"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Save Texture"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Auto"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
}

void SMainWindow::MakeToolsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		INVTEXT("Settings"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
}

void SMainWindow::MakeHelpMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		INVTEXT("Donate"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			FPlatformProcess::LaunchURL(TEXT("https://fmodel.app/donate"), nullptr, nullptr);
		}))
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Changelog"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Bugs Report"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			FPlatformProcess::LaunchURL(TEXT("https://github.com/iAmAsval/FModel/issues/new/choose"), nullptr, nullptr);
		}))
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("Discord Server"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			FPlatformProcess::LaunchURL(TEXT("https://fmodel.app/discord"), nullptr, nullptr);
		}))
	);
	MenuBuilder.AddMenuEntry(
		INVTEXT("About FModel 5"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction()
	);
}

void FFileTreeNode::AddEntry(const FString& sEntry, int wBegIndex)
{
	if (wBegIndex < sEntry.Len())
	{
		int32 wEndIndex = sEntry.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, wBegIndex);
		if (wEndIndex == -1)
		{
			wEndIndex = sEntry.Len();
		}
		FString sKey = sEntry.Mid(wBegIndex, wEndIndex - wBegIndex);
		if (sKey.Len())
		{
			TSharedPtr<FFileTreeNode>& oItem = Entries.FindOrAdd(sKey);
			if (!oItem.IsValid())
			{
				oItem = MakeShared<FFileTreeNode>();
				oItem->Path = sEntry.Left(wEndIndex);
				oItem->Parent = this;
			}
			oItem->AddEntry(sEntry, wEndIndex + 1);
		}
	}
}

TArray<TSharedPtr<FFileTreeNode>>* FFileTreeNode::GetEntries()
{
	if (EntriesList)
	{
		return EntriesList;
	}
	EntriesList = new TArray<TSharedPtr<FFileTreeNode>>();
	Entries.GenerateValueArray(*EntriesList);
	Algo::Sort(*EntriesList, [](const TSharedPtr<FFileTreeNode>& A, const TSharedPtr<FFileTreeNode>& B)
	{
		bool AIsFile = A->IsFile();
		bool BIsFile = B->IsFile();
		if (AIsFile != BIsFile)
		{
			return BIsFile;
		}
		return A->GetName() < B->GetName();
	});;
	return EntriesList;
}

TUniquePtr<FArchive> Read(const TSharedPtr<FFileTreeNode>& Item)
{
	const TCHAR* Filename = *Item->Path;
	if (IFileHandle* File = FFModelApp::Get().Provider->Read(Filename))
	{
		if (TUniquePtr<FArchive> Reader = MakeUnique<FArchiveFileReaderGeneric>(File, Filename, File->Size()))
		{
			return Reader;
		}
		else
		{
			UE_LOG(LogFModel, Warning, TEXT("Failed to read file '%s' error."), Filename);
		}
	}
	else
	{
		UE_LOG(LogFModel, Warning, TEXT("Failed to read file '%s' error."), Filename);
	}

	return nullptr;
}

TSharedRef<SMultiLineEditableTextBox> TextBox(const FString& S)
{
	return SNew(SMultiLineEditableTextBox)
		.Style(FEditorStyle::Get(), "Log.TextBox")
		.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
		.IsReadOnly(true)
		.Text(FText::FromString(S));
}

TSharedRef<SWidget> EmptyInTheMiddle(const FText Text)
{
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Text)
		];
}

TArray<FString> TextExtensions = {
	"ini",
	"json",
	"uplugin",
	"uproject"
};

TSharedRef<SWidget> PopulateTabContents(const TSharedPtr<FFileTreeNode>& Item)
{
	check(Item->IsFile());

	TUniquePtr<FArchive> Ar = Read(Item);
	if (!Ar)
	{
		FText Text = INVTEXT("Failed to load file");
		return EmptyInTheMiddle(Text);
	}

	FString Ext = FPaths::GetExtension(Item->Path).ToLower();
	if (TextExtensions.Contains(Ext))
	{
		FString Result;
		FFileHelper::LoadFileToString(Result, *Ar);
		return TextBox(Result);
	}

	return EmptyInTheMiddle(FText::Format(INVTEXT("Unsupported file type: {0}"), FText::FromString(Ext)));
}

void SMainWindow::Construct(const FArguments& Args)
{
	SWindow::Construct(
		SWindow::FArguments()
		.Title(INVTEXT("FModel"))
		.ClientSize(FVector2D(1440, 800))
	);

	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());
	MenuBarBuilder.AddPullDownMenu(
		INVTEXT("Directory"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SMainWindow::MakeDirectoryMenu)
	);
	MenuBarBuilder.AddPullDownMenu(
		INVTEXT("Assets"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SMainWindow::MakeAssetsMenu)
	);
	MenuBarBuilder.AddPullDownMenu(
		INVTEXT("Tools"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SMainWindow::MakeToolsMenu)
	);
	MenuBarBuilder.AddPullDownMenu(
		INVTEXT("Help"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &SMainWindow::MakeHelpMenu)
	);

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab).TabRole(MajorTab);
	TabManager = FGlobalTabmanager::Get()->NewTabManager(DockTab);
	TSharedRef<FWorkspaceItem> AppMenuGroup = TabManager->AddLocalWorkspaceMenuCategory(INVTEXT("FModel"));

	LoadingModeOptions = {
		MakeShared<ELoadingMode>(ELoadingMode::Single),
		MakeShared<ELoadingMode>(ELoadingMode::Multiple),
		MakeShared<ELoadingMode>(ELoadingMode::All),
		MakeShared<ELoadingMode>(ELoadingMode::AllButNew),
		MakeShared<ELoadingMode>(ELoadingMode::AllButModified),
	};

	BuildArchivesList();

	TabManager->RegisterTabSpawner("Archives", FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args)
	{
		TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.Label(INVTEXT("Archives"))
			.OnCanCloseTab_Lambda([] { return false; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 4, 4, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(STextBlock)
						.Text(INVTEXT("Loading Mode"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(ComboBox_LoadingMode, SComboBox<TSharedPtr<ELoadingMode>>)
						.OptionsSource(&LoadingModeOptions)
						.InitiallySelectedItem(LoadingModeOptions[2])
						.OnGenerateWidget_Lambda([](TSharedPtr<ELoadingMode> InItem)
						{
							return SNew(STextBlock).Text(FText::FromString(LexToString(*InItem)));
						})
						[
							SNew(STextBlock).Text_Lambda([this]
							{
								return FText::FromString(LexToString(*ComboBox_LoadingMode->GetSelectedItem()));
							})
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4, 4, 4, 4)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(INVTEXT("Load"))
					.OnClicked_Lambda([this]
					{
						FAES::FAESKey Key;
						HexToBytes(TEXT("DAE1418B289573D4148C72F3C76ABC7E2DB9CAA618A3EAF2D8580EB3A1BB7A63"), Key.Key);
						FFModelApp::Get().Provider->SubmitKey(FGuid(), Key);
						BuildFilesList();
						return FReply::Handled();
					})
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(List_Archives, SListView<TSharedPtr<FVfsEntry>>)
					.ListItemsSource(&Archives)
					.OnGenerateRow_Lambda([](TSharedPtr<FVfsEntry> InItem, const TSharedRef<STableViewBase>& InOwner) -> TSharedRef<ITableRow>
					{
						FNumberFormattingOptions Options;
						Options.MaximumFractionalDigits = 2;
						return SNew(STableRow<TSharedPtr<FVfsEntry>>, InOwner)
						.Padding(FMargin(6, 2))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0, 0, 4, 0)
							[
								SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icon.ArchiveEnabled"))
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1)
							[
								SNew(STextBlock).Text(FText::FromString(InItem->Name))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock).Text(FText::AsMemory(InItem->Size, &Options))
							]
						];
					})
				]
			];
		return Tab;
	}));

	TabManager->RegisterTabSpawner("Files", FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args)
	{
		TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.Label(INVTEXT("Files"))
			.OnCanCloseTab_Lambda([] { return false; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(Breadcrumb_Path, SBreadcrumbTrail<FFileTreeNode*>)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(Tree_Files, STreeView<TSharedPtr<FFileTreeNode>>)
					.TreeItemsSource(Files.GetEntries())
					.OnGenerateRow_Lambda([](TSharedPtr<FFileTreeNode> InItem, const TSharedRef<STableViewBase>& InOwner) -> TSharedRef<ITableRow>
					{
						return SNew(STableRow<TSharedPtr<FFileTreeNode>>, InOwner)
						[
							SNew(STextBlock).Text(FText::FromString(InItem->GetName()))
						];
					})
					.OnGetChildren_Lambda([](TSharedPtr<FFileTreeNode> Item, TArray<TSharedPtr<FFileTreeNode>>& OutChildren)
					{
						OutChildren = *Item->GetEntries();
					})
					.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FFileTreeNode> Item)
					{
						if (Item.IsValid() && Item->IsFile())
						{
							TSharedRef<SDockTab> Tab = SNew(SDockTab)
								.TabRole(DocumentTab)
								.Label(FText::FromString(Item->GetName()))
								[
									PopulateTabContents(Item)
								];
							TabManager->InsertNewDocumentTab("Document", FTabManager::ESearchPreference::RequireClosedTab, Tab);
						}
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FFileTreeNode> InItem, ESelectInfo::Type SelectInfo)
					{
						Breadcrumb_Path->ClearCrumbs();
						TArray<FFileTreeNode*> NodesToRoot;
						FFileTreeNode* Current = InItem.Get();
						while (Current && Current->Path.Len())
						{
							NodesToRoot.Add(Current);
							Current = Current->Parent;
						}
						for (int32 i = NodesToRoot.Num(); i--;)
						{
							Breadcrumb_Path->PushCrumb(FText::FromString(NodesToRoot[i]->GetName()), NodesToRoot[i]);
						}
					})
				]
			];
		return Tab;
	}));

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("FModel_v1.0")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.25f)
				->AddTab("Archives", ETabState::OpenedTab)
				->AddTab("Files", ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.75f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.75f)
					->AddTab("Document", ETabState::ClosedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetHideTabWell(true)
					->SetSizeCoefficient(0.25f)
					->AddTab("OutputLog", ETabState::OpenedTab)
				)
			)
		);

	SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MenuBarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			TabManager->RestoreFrom(Layout, TSharedPtr<SWindow>()).ToSharedRef()
		]
	);

	// TabManager->TryInvokeTab(FName("WidgetReflector"));
}

void SMainWindow::BuildArchivesList()
{
	Archives.Empty();

	FRegexPattern ArchiveNamePattern("^(?!global|pakchunk.+optional\\-).+(pak|utoc)$", ERegexPatternFlags::CaseInsensitive);
	FVfsPlatformFile* Provider = FFModelApp::Get().Provider;
	for (const FVfs& Vfs : Provider->UnloadedVfs)
	{
		FRegexMatcher Matcher(ArchiveNamePattern, Vfs.GetName());
		if (Vfs.GetSize() > 365 && Matcher.FindNext())
		{
			Archives.Add(MakeShared<FVfsEntry>(Vfs));
		}
	}

	// chunk id then name
	Algo::Sort(Archives, [](const TSharedPtr<FVfsEntry>& A, const TSharedPtr<FVfsEntry>& B)
	{
		int32 AChunkId = A->GetChunkId();
		int32 BChunkId = B->GetChunkId();
		if (AChunkId != BChunkId)
		{
			return AChunkId < BChunkId;
		}
		int32 ASplitNumber = A->GetSplitNumber();
		int32 BSplitNumber = B->GetSplitNumber();
		if (ASplitNumber != BSplitNumber)
		{
			return ASplitNumber < BSplitNumber;
		}
		return A->Name < B->Name;
	});
}

void SMainWindow::BuildFilesList()
{
	Files.Reset();
	TArray<FVfs> VfsToLoad;
	ELoadingMode LoadingMode = *ComboBox_LoadingMode->GetSelectedItem();
	auto Provider = FFModelApp::Get().Provider;
	if (LoadingMode == ELoadingMode::All)
	{
		for (const FVfs& Vfs : Provider->MountedVfs)
		{
			VfsToLoad.Add(Vfs);
		}
	}
	if (!VfsToLoad.Num())
	{
		UpdateFilesList();
		return;
	}
	for (const FVfs& Vfs : VfsToLoad)
	{
		FPakFile& PakFile = *Vfs.PakFile;
		for (FPakFile::FPakEntryIterator It(PakFile, false); It; ++It)
		{
			if (const FString* Filename = It.TryGetFilename())
			{
				Files.AddEntry(Vfs.GetMountPoint() / *Filename);
			}
		}
	}
	UpdateFilesList();
}

void SMainWindow::UpdateFilesList()
{
	Tree_Files->SetTreeItemsSource(Files.GetEntries());
	// @todo: Empty text
}

