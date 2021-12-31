#pragma once

#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

class SKeychainWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SKeychainWindow) { }
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SWindow::Construct(
			SWindow::FArguments()
			.Title(INVTEXT("AES Manager"))
			.ClientSize(FVector2D(700, 400))
			.SizingRule(ESizingRule::FixedSize)
			.SupportsMaximize(false)
		);
		SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(4.0f, 4.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16.0f))
				.Text(INVTEXT("What to do?"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 4.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.Text(INVTEXT("In order to decipher files' information, an AES key, in most cases, is needed. Here you can set the key for your static and dynamic files. If you don't know what key to use for your set game, simply Google it. Keys must start with \"0x\" and contains 64 more characters."))
			]
		);
	}
};
