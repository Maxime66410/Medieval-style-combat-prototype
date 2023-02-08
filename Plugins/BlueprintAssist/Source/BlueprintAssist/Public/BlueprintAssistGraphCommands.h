// Copyright 2021 fpwong. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

class BLUEPRINTASSIST_API FBAGraphCommands final : public TCommands<FBAGraphCommands>
{
public:
	FBAGraphCommands() : TCommands<FBAGraphCommands>(
		TEXT("BlueprintAssistGraphCommands"),
		NSLOCTEXT("Contexts", "BlueprintAssistGraphCommands", "Blueprint Assist Graph Commands"),
		NAME_None,
		FEditorStyle::Get().GetStyleSetName()
	) {}

	TSharedPtr<FUICommandInfo> GenerateGetter;

	TSharedPtr<FUICommandInfo> GenerateSetter;

	TSharedPtr<FUICommandInfo> GenerateGetterAndSetter;

	virtual void RegisterCommands() override;
};
