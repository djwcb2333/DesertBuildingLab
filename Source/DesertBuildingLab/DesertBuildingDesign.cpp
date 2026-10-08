#include "DesertBuildingDesign.h"
#include "DesertBuildingStyle.h"

void UDesertBuildingDesign::CaptureFrom(const ADesertBuilding* Source)
{
    if (!Source) return;
    Cells = Source->Cells;
    RoomAppearanceOverrides = Source->RoomAppearanceOverrides;
    FormatVersion = 5;
    Blocks = Source->Blocks;
    RoofOpenings = Source->RoofOpenings;
    RoofDecorations = Source->RoofDecorations;
    Style = Source->Style;
    CellSize = Source->CellSize;
    FloorHeight = Source->FloorHeight;
    FoundationDepth = Source->FoundationDepth;
    Seed = Source->Seed;
    bShowWoodBeams = Source->bShowWoodBeams;
    bEnableAwning = Source->bEnableAwning;
    bAutoSupportColumns = Source->bAutoSupportColumns;
    SupportTraceDistance = Source->SupportTraceDistance;
    bUseEntranceOverride = Source->bUseEntranceOverride;
    EntranceOverrideCell = Source->EntranceOverrideCell;
    EntranceOverrideSide = Source->EntranceOverrideSide;
}

void UDesertBuildingDesign::ApplyTo(ADesertBuilding* Target, bool bRebuild) const
{
    if (!Target) return;
    Target->Cells = Cells;
    Target->RoomAppearanceOverrides = RoomAppearanceOverrides;
    Target->Blocks = Blocks;
    Target->RoofOpenings = RoofOpenings;
    Target->RoofDecorations = RoofDecorations;
    Target->Style = Style;
    Target->CellSize = CellSize;
    Target->FloorHeight = FloorHeight;
    Target->FoundationDepth = FoundationDepth;
    Target->Seed = Seed;
    Target->bShowWoodBeams = bShowWoodBeams;
    Target->bEnableAwning = bEnableAwning;
    Target->bAutoSupportColumns = bAutoSupportColumns;
    Target->SupportTraceDistance = SupportTraceDistance;
    Target->bUseEntranceOverride = bUseEntranceOverride;
    Target->EntranceOverrideCell = EntranceOverrideCell;
    Target->EntranceOverrideSide = EntranceOverrideSide;
    Target->DesignAsset = const_cast<UDesertBuildingDesign*>(this);
    Target->AppliedDesignRevision = Revision;
    if (bRebuild && !Target->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) Target->Rebuild();
}
