#include "FModelApp.h"
#include "Editor/EditorStyle/Public/EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISourceCodeAccessModule.h"
#include "Interfaces/IEditorStyleModule.h"
#include "OutputLogModule.h"
#include "PakFile/Public/IPlatformFilePak.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "UI/Widgets/SMainWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Testing/SStarshipSuite.h"
#include "Widgets/Testing/STestSuite.h"

IMPLEMENT_APPLICATION(FModel, "FModel");
DEFINE_LOG_CATEGORY(LogFModel);

#define LOCTEXT_NAMESPACE "FModel"

namespace WorkspaceMenu
{
	TSharedRef<FWorkspaceItem> DeveloperMenu = FWorkspaceItem::NewGroup(LOCTEXT("DeveloperMenu", "Developer"));
}

int RunApplication(const TCHAR* CommandLine)
{
	FTaskTagScope TaskTagScope(ETaskTag::EGameThread);

	/*custom*/ FPlatformMisc::SetOverrideProjectDir(TEXT("../../"));

	// start up the main loop
	GEngineLoop.PreInit(CommandLine);

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();

	// Tell the module manager it may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();


	// crank up a normal Slate application using the platform's standalone renderer
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

	FSlateApplication::InitHighDPI(true);

	// Load the source code access module
	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");

	// Manually load in the source code access plugins, as standalone programs don't currently support plugins.
#if PLATFORM_MAC
	IModuleInterface& XCodeSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>("XCodeSourceCodeAccess");
	SourceCodeAccessModule.SetAccessor("XCodeSourceCodeAccess");
#elif PLATFORM_WINDOWS
	IModuleInterface& VisualStudioSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>("VisualStudioSourceCodeAccess");
	SourceCodeAccessModule.SetAccessor("VisualStudioSourceCodeAccess");
#endif

	// Set the application title
	FGlobalTabmanager::Get()->SetApplicationTitle(INVTEXT("FModel"));

	// Load modules
	FModuleManager::LoadModuleChecked<IEditorStyleModule>("EditorStyle"); // For output log
	FModuleManager::LoadModuleChecked<FOutputLogModule>("OutputLog");
	FModuleManager::LoadModuleChecked<ISlateReflectorModule>("SlateReflector").RegisterTabSpawner(WorkspaceMenu::DeveloperMenu);

	// Set up styles
	TSharedRef<FSlateStyleSet> StyleRef = MakeShareable(new FSlateStyleSet("FModelStyle"));
	FSlateStyleSet& Style = StyleRef.Get();
	Style.SetParentStyleName(FEditorStyle::GetStyleSetName());
	Style.Set("AppIcon", new FSlateImageBrush(FPaths::ProjectContentDir() / TEXT("Icons/FModel-48.png"), FVector2D(45.0f, 45.0f)));
	Style.Set("AppIcon.Small", new FSlateImageBrush(FPaths::ProjectContentDir() / TEXT("Icons/FModel-24.png"), FVector2D(24.0f, 24.0f)));
	Style.Set("Icon.ArchiveDisabled", new FSlateImageBrush(FPaths::ProjectContentDir() / TEXT("Icons/archive_disabled.png"), FVector2D(16.0f, 16.0f)));
	Style.Set("Icon.ArchiveEnabled", new FSlateImageBrush(FPaths::ProjectContentDir() / TEXT("Icons/archive_enabled.png"), FVector2D(16.0f, 16.0f)));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleRef);
	FAppStyle::SetAppStyleSetName(StyleRef->GetStyleSetName());
	//RestoreStarshipSuite();

	// if (FParse::Param(FCommandLine::Get(), TEXT("testsuite")))
	{
		// RestoreSlateTestSuite();
	}

	// Initialize singleton
	FFModelApp::Get();

	// Open main window
	FSlateApplication::Get().AddWindow(SNew(SMainWindow));

#if WITH_SHARED_POINTER_TESTS
	SharedPointerTesting::TestSharedPointer<ESPMode::NotThreadSafe>();
	SharedPointerTesting::TestSharedPointer<ESPMode::ThreadSafe>();
#endif

	// loop while the server does the rest
	while (!IsEngineExitRequested())
	{
		BeginExitIfRequested();

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FStats::AdvanceFrame(false);
		FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();		
		FPlatformProcess::Sleep(0.01);

		GFrameCounter++;
	}

	FCoreDelegates::OnExit.Broadcast();
	FSlateApplication::Shutdown();
	FModuleManager::Get().UnloadModulesAtShutdown();

	GEngineLoop.AppPreExit();
	GEngineLoop.AppExit();

	return 0;
}

#undef LOCTEXT_NAMESPACE
