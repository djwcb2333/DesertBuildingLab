#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "EditorUndoClient.h"
#include "DesertBuilding.h"
#include "AdvancedPreviewScene.h"
#include "DesertBuildingModulePreviewData.h"

class SDesertBuildingPreviewViewport;
class UDesertBuildingDesign;
class UDesertBuildingStyle;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterial;
class UMaterialInstanceDynamic;
class IDetailsView;
class SDesertModulePreviewViewport;
class FAssetThumbnail;
class FAssetThumbnailPool;

/** 持久素材与编辑草稿明确分开。点击只修改预览，保存才传播到绑定实例。 */
class SDesertBuildingDesigner : public SCompoundWidget, public FGCObject, public FEditorUndoClient
{
public:
    SLATE_BEGIN_ARGS(SDesertBuildingDesigner) {} SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual ~SDesertBuildingDesigner() override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("SDesertBuildingDesigner"); }
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

    ADesertBuilding* GetPreviewBuilding() const { return PreviewBuilding.Get(); }
    int32 GetEditFloor() const { return EditFloor; }
    TOptional<FIntVector> GetSelectedRoomCell() const { return SelectedRoomCell; }
    void HandleCellClick(FIntVector Cell, bool bDelete);
    void SetHoveredCell(TOptional<FIntVector> Cell);
    FLinearColor GetHoverColor() const;
    const TArray<FBox>& GetHoverBounds() const { return HoverCheck.LocalBounds; }
    void FocusFromViewport();
    static ADesertBuilding* FindSelectedBuilding();
    static int32 RefreshLinkedInstances(UDesertBuildingDesign* Design);
    bool LoadDesignSafely(UDesertBuildingDesign* Design,FString& Message);
    bool SelectModulePreview(int32 Tool,EDesertStairLayout Layout,int32 VariantIndex,int32 InFacing);
    bool SetPotPlacementSelection(EDesertPotPlacement Position);
    FDesertModulePreviewData GetDisplayedModulePreviewData() const {return DisplayedModuleData;}

private:
    // 0房间；1屋顶棚亭；2穹顶；3瓦罐组；4篷布支架；5楼梯；8外围散石；9斜顶墙冠。6/7保留地基/柱模型槽。
    int32 ActiveTool = 0;
    int32 EditFloor = 0;
    int32 Facing = 0;
    EDesertStairLayout StairLayout = EDesertStairLayout::AlongWall;
    int32 AwningVariantIndex = -1;
    int32 PotVariantIndex = -1;
    EDesertPotPlacement PotPlacement = EDesertPotPlacement::Automatic;
    TOptional<FIntVector> SelectedPotCell;
    TOptional<FIntVector> HoveredCell;
    TOptional<FIntVector> SelectedRoomCell;
    FDesertPlacementCheck HoverCheck;
    bool bHoverNeedsRefresh = true;
    TObjectPtr<ADesertBuilding> GhostRecipeBuilding = nullptr;
    TObjectPtr<UMaterial> GhostMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> GhostDynamicMaterial = nullptr;
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> GhostComponents;
    FString LastAction;
    bool bDraftDirty = false;
    TObjectPtr<ADesertBuilding> PreviewBuilding = nullptr;
    TObjectPtr<UDesertBuildingStyle> DraftStyle = nullptr;
    TObjectPtr<UDesertBuildingDesign> LoadedDesign = nullptr;
    TObjectPtr<UStaticMeshComponent> PreviewGround = nullptr;
    TUniquePtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<SDesertBuildingPreviewViewport> PreviewViewport;
    TSharedPtr<IDetailsView> AdvancedStyleDetails;
    TSharedPtr<SDesertModulePreviewViewport> ModulePreviewViewport;
    TSharedPtr<FAssetThumbnailPool> ModuleThumbnailPool;
    TMap<int32,TSharedPtr<FAssetThumbnail>> ModuleToolThumbnails;
    TArray<TSharedPtr<FAssetThumbnail>> ModuleVariantThumbnails;
    FDesertModulePreviewData DisplayedModuleData;
    bool bModulePreviewNeedsRefresh=true;
    FIntVector LastModuleContextCell=FIntVector::ZeroValue;
    int32 LastModuleResolvedVariant=INDEX_NONE;

    TSharedRef<SWidget> MakeToolButton(int32 Tool, const FText& Label);
    UStaticMesh* GetToolThumbnailMesh(int32 Tool) const;
    TSharedRef<SWidget> MakeModulePreviewPanel();
    void RefreshModulePreview();
    FText GetLegacyStairText() const;
    FReply MigrateLegacyStairs();
    TSharedRef<SWidget> MakeMeshPicker(int32 Slot, const FText& Label);
    UStaticMesh* GetSlotMesh(int32 Slot) const;
    void SetSlotMesh(int32 Slot, UStaticMesh* Mesh);
    FText GetMeshContract(int32 Slot) const;
    FText GetAssetCheckText() const;
    FText GetAwningVariantText() const;
    TSharedRef<SWidget> MakeAwningVariantMenu();
    TSharedRef<SWidget> MakePotPlacementMenu();
    FText GetPotPlacementText() const;
    bool HasPlacedPot(FIntVector Cell) const;
    TSharedRef<SWidget> MakeRoofDressingPanel();
    TSharedRef<SWidget> MakeRoofDressingMenu();
    TSharedRef<SWidget> MakeStairLayoutMenu();
    FText GetStairLayoutText() const;
    TSharedRef<SWidget> MakeRoomAppearancePanel();
    FReply ApplySelectedRoomAppearance(EDesertRoomDoorMode Mode, bool bChangeDoor, int32 WindowSide=-1, bool bToggleWindows=false);
    FText GetStatusText() const;
    FText GetFloorText() const;
    FText GetFacingText() const;
    FText GetHoverText() const;
    FText GetInvalidDraftText() const;
    FDesertBlockPlacement MakeProposal(FIntVector Cell) const;
    void RefreshHoverPreview();
    void ClearGhostGeometry();
    void AddGhostMesh(UStaticMesh* Mesh, const FTransform& Placement);
    void AddGhostRecipe(EDesertModuleKind Kind, const FTransform& Placement, FVector Dimensions, int32 Steps, bool bWithProps,
        UStaticMesh* ResolvedMesh = nullptr);
    FReply ClearInvalidDraft();
    void RebuildPreview();
    void ReplaceDraftStyle(const UDesertBuildingStyle* Source);
    void LoadDesign(UDesertBuildingDesign* Design);
    FReply NewDraft();
    FReply LoadSelected();
    FReply LoadAssetDialog();
    FReply ApplySelected();
    FReply PlaceInLevel();
    FReply SaveAs();
    FReply SaveUpdate();
    FReply Undo();
    FReply Redo();
    FReply FocusPreview();
    bool SaveDesign(bool bUpdate);
    void CopyDraftTo(ADesertBuilding* Destination, UDesertBuildingStyle* Style) const;
};
