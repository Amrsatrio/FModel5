#pragma once

#include "EditorStyleSet.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/KeyChainUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

// FKeyChain GKeyChain;

struct FKeyChainEntry
{
	FString Name, Key;

	FKeyChainEntry(const FString& Name, const FString& Key)
		: Name(Name),
		  Key(Key)
	{
	}
};

class SKeychainWindow : public SWindow
{
	TSharedPtr<SScrollBox> ScrollBox_EncryptionKeys;
	TSharedPtr<SGridPanel> Grid_EncryptionKeys;
	TSharedPtr<SButton> Button_Refresh;
	TSharedPtr<STextBlock> Text_Version;
	TSharedPtr<SButton> Button_Apply;
	TArray<TSharedPtr<FKeyChainEntry>> Entries;

public:
	SLATE_BEGIN_ARGS(SKeychainWindow) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SWindow::Construct(SWindow::FArguments()
			.Title(INVTEXT("AES Manager"))
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.SizingRule(ESizingRule::Autosized)
			.SupportsMaximize(false)
		);
		SetContent(
			SNew(SVerticalBox)

			// Header
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredWidth(576)
				.Padding(FMargin(10, 5, 10, 10))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.Text(INVTEXT("What to do?"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
						.Text(INVTEXT("In order to decipher files' information, an AES key, in most cases, is needed. Here you can set the key for your static and dynamic files. If you don't know what key to use for your set game, simply Google it. Keys must start with \"0x\" and contains 64 more characters."))
					]
				]
			]

			// Encryption keys list
			+ SVerticalBox::Slot()
			.FillHeight(1)
			.MaxHeight(350)
			[
				SAssignNew(ScrollBox_EncryptionKeys, SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SBox)
					.Padding(FMargin(8))
					[
						SAssignNew(Grid_EncryptionKeys, SGridPanel)
					]
				]
			]

			// Action buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(16, 12))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(Button_Refresh, SButton)
						.Text(INVTEXT("Refresh"))
						.OnClicked(this, &SKeychainWindow::OnRefresh)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0, 0, 0)
					[
						SAssignNew(Text_Version, STextBlock)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(Button_Apply, SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
						.Text(INVTEXT("Apply"))
						.OnClicked(this, &SKeychainWindow::OnApply)
					]
				]
			]
		);
		ScrollBox_EncryptionKeys->SetScrollBarRightClickDragAllowed(true);
	}

	FReply OnRefresh()
	{
		Button_Refresh->SetEnabled(false);
		FHttpRequestPtr HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL("https://benbot.app/api/v2/aes");
		HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			Button_Refresh->SetEnabled(true);
			if (!bWasSuccessful)
			{
				return;
			}

			FString ResponseStr = Response->GetContentAsString();
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
			if (FJsonSerializer::Deserialize(Reader, JsonObject))
			{
				FString Version = JsonObject->GetStringField("version");
				Text_Version->SetText(FText::FromString(Version));
				FString MainKey = JsonObject->GetStringField("mainKey");
				TArray<TSharedPtr<FJsonValue>> SecondaryKeys = JsonObject->GetArrayField("dynamicKeys");
				Entries.Reset(1 + SecondaryKeys.Num());
				Entries.Add(MakeShared<FKeyChainEntry>("Primary Key", MainKey));
				for (TSharedPtr<FJsonValue> SecondaryKeyValue : SecondaryKeys)
				{
					TSharedPtr<FJsonObject> SecondaryKey = SecondaryKeyValue->AsObject();
					FString Guid = SecondaryKey->GetStringField("guid");
					FString FileName = SecondaryKey->GetStringField("fileName");
					FString Key = SecondaryKey->GetStringField("key");
					int32 ChunkId = FPlatformMisc::GetPakchunkIndexFromPakFile(FileName);
					Entries.Add(MakeShared<FKeyChainEntry>(FString::Printf(TEXT("Chunk %d"), ChunkId), Key));
				}
				Grid_EncryptionKeys->ClearChildren();
				for (int32 i = 0; i < Entries.Num(); ++i)
				{
					TSharedPtr<FKeyChainEntry>& Entry = Entries[i];
					Grid_EncryptionKeys->AddSlot(0, i)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(0, 0, 12, 4)
					[
						SNew(STextBlock).Text(FText::FromString(Entry->Name))
					];
					Grid_EncryptionKeys->AddSlot(1, i)
					.Padding(0, 0, 0, 4)
					[
						SNew(SEditableTextBox)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
						.MinDesiredWidth(540)
						.Text(FText::FromString(Entry->Key))
					];
				}
			}
		});
		HttpRequest->ProcessRequest();
		return FReply::Handled();
	}

	FReply OnApply()
	{
		return FReply::Handled();
	}
};
