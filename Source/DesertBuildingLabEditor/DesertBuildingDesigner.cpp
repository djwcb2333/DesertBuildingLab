#include "DesertBuildingDesigner.h"
#include "DesertBuildingDesign.h"
#include "DesertBuildingStyle.h"
#include "DesertBuildingEditorLibrary.h"
#include "DesertBuildingDefaults.h"
#include "DesertBuildingModulePreviewViewport.h"
#include "AssetThumbnail.h"
#include "Framework/Application/SlateApplication.h"
#include "AdvancedPreviewScene.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Settings/EditorViewportSettings.h"
#include "SceneManagement.h"
#include "InputKeyEventArgs.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "DesertBuildingDesigner"

class FDesertBuildingPreviewClient : public FEditorViewportClient
{
public:
    FDesertBuildingPreviewClient(FAdvancedPreviewScene* Scene, const TSharedRef<SEditorViewport>& Widget,
        const TWeakPtr<SDesertBuildingDesigner>& InDesigner)
        : FEditorViewportClient(nullptr, Scene, Widget), Designer(InDesigner)
    {
        SetViewLocation(FVector(1850,-2200,1850));
        SetViewRotation(FRotator(-31,130,0));
        SetRealtime(true);
        EngineShowFlags.SetGrid(false);
        EngineShowFlags.SetSelectionOutline(false);
        // 默认透视相机；右键/鼠标轴/WASD/QE/滚轮由UE自己的导航实现处理。
        bUsingOrbitCamera = false;
    }

    bool PickCell(FIntVector& Result)
    {
        TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin();
        if (!bMouseInsideViewport || !Owner || !Owner->GetPreviewBuilding() || !Viewport) return false;
        const FIntPoint Mouse(Viewport->GetMouseX(), Viewport->GetMouseY());
        const FIntPoint Size = Viewport->GetSizeXY();
        if (Mouse.X < 0 || Mouse.Y < 0 || Mouse.X >= Size.X || Mouse.Y >= Size.Y) return false;
        const FViewportCursorLocation Cursor = GetCursorWorldLocationFromMousePos();
        const double Denominator = Cursor.GetDirection().Z;
        if (FMath::Abs(Denominator) < UE_SMALL_NUMBER) return false;
        ADesertBuilding* Building = Owner->GetPreviewBuilding();
        const double Height = Owner->GetEditFloor() * Building->FloorHeight;
        const double Distance = (Height-Cursor.GetOrigin().Z) / Denominator;
        if (Distance <= 0) return false;
        const FVector Hit = Cursor.GetOrigin() + Cursor.GetDirection() * Distance;
        Result = FIntVector(FMath::FloorToInt(Hit.X/Building->CellSize),
            FMath::FloorToInt(Hit.Y/Building->CellSize), Owner->GetEditFloor());
        return Result.X>=-3 && Result.X<=3 && Result.Y>=-3 && Result.Y<=3 && Result.Z>=0 && Result.Z<=6;
    }

    virtual void MouseEnter(FViewport* InViewport,int32 X,int32 Y) override
    {
        bMouseInsideViewport=true;
        FEditorViewportClient::MouseEnter(InViewport,X,Y);
    }

    virtual void MouseLeave(FViewport* InViewport) override
    {
        bMouseInsideViewport=false;
        // 编辑器FSceneViewport保留最后的鼠标像素缓存；不能拿它继续显示旧格候选。
        if(TSharedPtr<SDesertBuildingDesigner> Owner=Designer.Pin()) Owner->SetHoveredCell({});
        FEditorViewportClient::MouseLeave(InViewport);
    }

    virtual bool InputKey(const FInputKeyEventArgs& Args) override
    {
        const bool bCameraHeld = Viewport && Viewport->KeyState(EKeys::RightMouseButton);
        if (Args.Key == EKeys::RightMouseButton)
        {
            if (Args.Event == IE_Pressed)
                if (TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin()) Owner->SetHoveredCell({});
            return FEditorViewportClient::InputKey(Args);
        }
        if (!bCameraHeld && !IsAltPressed() && !IsCtrlPressed()
            && (Args.Key == EKeys::LeftMouseButton || Args.Key == EKeys::Delete))
        {
            if (Args.Event == IE_Pressed)
            {
                FIntVector Cell;
                if (PickCell(Cell))
                    if (TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin())
                        Owner->HandleCellClick(Cell, Args.Key == EKeys::Delete || IsShiftPressed());
            }
            return true;
        }
        if (!bCameraHeld && Args.Key == EKeys::F && Args.Event == IE_Pressed)
            if (TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin()) { Owner->FocusFromViewport(); return true; }
        return FEditorViewportClient::InputKey(Args);
    }

    virtual void Tick(float DeltaSeconds) override
    {
        FEditorViewportClient::Tick(DeltaSeconds);
        if (TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin())
        {
            FIntVector Cell;
            const bool bCameraHeld = Viewport && (Viewport->KeyState(EKeys::RightMouseButton) || Viewport->KeyState(EKeys::MiddleMouseButton) || IsAltPressed());
            Owner->SetHoveredCell(!bCameraHeld && PickCell(Cell) ? TOptional<FIntVector>(Cell) : TOptional<FIntVector>());
        }
    }

    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin();
        if (!Owner || !Owner->GetPreviewBuilding()) return;
        const ADesertBuilding* Building = Owner->GetPreviewBuilding();
        const float Step = Building->CellSize;
        const float Height = Owner->GetEditFloor()*Building->FloorHeight + 2;
        for (int32 I=-3; I<=4; ++I)
        {
            PDI->DrawLine(FVector(I*Step,-3*Step,Height),FVector(I*Step,4*Step,Height),
                FLinearColor(0.25f,0.65f,0.8f,1), SDPG_Foreground, 0.7f);
            PDI->DrawLine(FVector(-3*Step,I*Step,Height),FVector(4*Step,I*Step,Height),
                FLinearColor(0.25f,0.65f,0.8f,1), SDPG_Foreground, 0.7f);
        }
        if (Owner->GetSelectedRoomCell().IsSet())
        {
            const FIntVector Cell=Owner->GetSelectedRoomCell().GetValue();
            const FVector Min(Cell.X*Step,Cell.Y*Step,Cell.Z*Building->FloorHeight);
            DrawWireBox(PDI,FBox(Min,Min+FVector(Step,Step,Building->FloorHeight)),FLinearColor(1.f,.72f,.12f),SDPG_Foreground,2.4f);
        }
        for (const FBox& Bounds : Owner->GetHoverBounds())
        {
            if (Bounds.IsValid) DrawWireBox(PDI,Bounds,Owner->GetHoverColor(),SDPG_Foreground,1.6f);
        }
    }
private:
    TWeakPtr<SDesertBuildingDesigner> Designer;
    bool bMouseInsideViewport=false;
};

class SDesertBuildingPreviewViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SDesertBuildingPreviewViewport) {}
        SLATE_ARGUMENT(FAdvancedPreviewScene*, Scene)
        SLATE_ARGUMENT(TWeakPtr<SDesertBuildingDesigner>, Designer)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Scene = Args._Scene;
        Designer = Args._Designer;
        SEditorViewport::Construct(SEditorViewport::FArguments());
    }
    void FocusBuilding()
    {
        if (TSharedPtr<SDesertBuildingDesigner> Owner = Designer.Pin())
            if (ADesertBuilding* Building = Owner->GetPreviewBuilding())
            {
                FBox Bounds = Building->GetComponentsBoundingBox(true);
                if (!Bounds.IsValid) Bounds = FBox(FVector(-500,-500,0),FVector(500,500,600));
                GetViewportClient()->FocusViewportOnBox(Bounds.ExpandBy(180),true);
            }
    }
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    {
        return MakeShared<FDesertBuildingPreviewClient>(Scene,SharedThis(this),Designer);
    }
private:
    FAdvancedPreviewScene* Scene = nullptr;
    TWeakPtr<SDesertBuildingDesigner> Designer;
};

static void DesertCopyStyle(const UDesertBuildingStyle* Source, UDesertBuildingStyle* Destination)
{
    if (!Source || !Destination) return;
    for (TFieldIterator<FProperty> It(UDesertBuildingStyle::StaticClass()); It; ++It)
        if (It->GetOwnerClass() == UDesertBuildingStyle::StaticClass())
            It->CopyCompleteValue_InContainer(Destination,Source);
}

void SDesertBuildingDesigner::Construct(const FArguments& InArgs)
{
    ModuleThumbnailPool=MakeShared<FAssetThumbnailPool>(64);
    PreviewScene = MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues()
        .SetCreatePhysicsScene(true).SetTransactional(true).SetEditor(true));
    PreviewScene->SetFloorVisibility(false,true);
    PreviewScene->SetEnvironmentVisibility(false,true);
    // 隐藏的引擎预览地板不参与支撑；只使用下面明确创建的可见地面。
    if(const UStaticMeshComponent* Floor=PreviewScene->GetFloorMeshComponent())
        const_cast<UStaticMeshComponent*>(Floor)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewGround = NewObject<UStaticMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient);
    PreviewGround->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    PreviewGround->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PreviewGround->SetCollisionResponseToAllChannels(ECR_Block);
    PreviewGround->SetMobility(EComponentMobility::Movable);
    PreviewScene->AddComponent(PreviewGround,FTransform(FRotator::ZeroRotator,FVector(150,150,-55),FVector(24,24,1)));
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags = RF_Transient | RF_Transactional;
    PreviewBuilding = PreviewScene->GetWorld()->SpawnActor<ADesertBuilding>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    // 规则已从Cells/Blocks求建筑承托，只让射线采样预览地面，避免候选打到旧柱/楼梯。
    PreviewBuilding->SetActorEnableCollision(false);
    PreviewBuilding->Cells.Empty();
    PreviewBuilding->Blocks.Empty();
    PreviewBuilding->RoofOpenings.Empty();
    PreviewBuilding->bEnableAwning = false;
    Spawn.ObjectFlags = RF_Transient;
    GhostRecipeBuilding = PreviewScene->GetWorld()->SpawnActor<ADesertBuilding>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    GhostRecipeBuilding->SetActorEnableCollision(false);
    GhostRecipeBuilding->SetIsTemporarilyHiddenInEditor(true);
    GhostRecipeBuilding->SetActorHiddenInGame(true);
    // 只存在于独立预览世界的材质与网格，不进入素材、关卡或建筑统计。
    GhostMaterial = NewObject<UMaterial>(GetTransientPackage(),NAME_None,RF_Transient);
    GhostMaterial->BlendMode = BLEND_Translucent;
    GhostMaterial->TwoSided = true;
    GhostMaterial->SetShadingModel(MSM_Unlit);
    UMaterialExpressionVectorParameter* Color = NewObject<UMaterialExpressionVectorParameter>(GhostMaterial);
    Color->ParameterName = TEXT("PlacementColor");
    Color->DefaultValue = FLinearColor(0.12f,0.95f,0.38f);
    Color->Material = GhostMaterial;
    Color->UpdateMaterialExpressionGuid(true,false);
    Color->UpdateParameterGuid(true,false);
    UMaterialExpressionScalarParameter* Opacity = NewObject<UMaterialExpressionScalarParameter>(GhostMaterial);
    Opacity->ParameterName = TEXT("PlacementOpacity");
    Opacity->DefaultValue = 0.32f;
    Opacity->Material = GhostMaterial;
    Opacity->UpdateMaterialExpressionGuid(true,false);
    Opacity->UpdateParameterGuid(true,false);
    GhostMaterial->GetExpressionCollection().AddExpression(Color);
    GhostMaterial->GetExpressionCollection().AddExpression(Opacity);
    GhostMaterial->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
    GhostMaterial->GetEditorOnlyData()->Opacity.Connect(0,Opacity);
    GhostMaterial->SetMaterialUsage(MATUSAGE_InstancedStaticMeshes);
    GhostMaterial->PostEditChange();
    GhostDynamicMaterial = UMaterialInstanceDynamic::Create(GhostMaterial,GetTransientPackage());
    UDesertBuildingStyle* DefaultStyle=DesertLoadDefaultBuildingStyle();
    ReplaceDraftStyle(DefaultStyle);
    RebuildPreview();
    if (GEditor) GEditor->RegisterForUndo(this);

    FPropertyEditorModule& Properties = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs DetailArgs;
    DetailArgs.bUpdatesFromSelection = false;
    DetailArgs.bLockable = false;
    DetailArgs.bAllowSearch = true;
    DetailArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    AdvancedStyleDetails = Properties.CreateDetailView(DetailArgs);
    AdvancedStyleDetails->SetObject(DraftStyle);
    AdvancedStyleDetails->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
    {
        bDraftDirty = true;
        LastAction = TEXT("已修改草稿美术配置；保存后才更新素材。");
        RebuildPreview();
    });

    ChildSlot
    [
        SNew(SSplitter)
        + SSplitter::Slot().Value(0.29f)
        [
            SNew(SBorder).Padding(10)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[MakeModulePreviewPanel()]
                + SVerticalBox::Slot().FillHeight(1)
                [ SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
                    [ SNew(STextBlock).Text(LOCTEXT("Intro","建筑设计器\n1 选择组合块和模型\n2 点击预览格子搭建\n3 保存素材，再拖入场景")).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("New","新建空白草稿")).OnClicked(this,&SDesertBuildingDesigner::NewDraft) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("LoadAsset","载入建筑素材…")).OnClicked(this,&SDesertBuildingDesigner::LoadAssetDialog) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("LoadSelected","载入场景中选中的建筑")).OnClicked(this,&SDesertBuildingDesigner::LoadSelected) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(STextBlock).Text(LOCTEXT("StyleLabel","载入美术风格配置（复制到草稿，保留当前布局）")).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SObjectPropertyEntryBox).AllowedClass(UDesertBuildingStyle::StaticClass())
                        .AllowClear(false).OnObjectChanged_Lambda([this](const FAssetData& Asset)
                        { ReplaceDraftStyle(Cast<UDesertBuildingStyle>(Asset.GetAsset())); bDraftDirty=true; RebuildPreview(); }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(STextBlock).Text(LOCTEXT("Choose","选择要放置的组合块")).Font(FCoreStyle::GetDefaultFontStyle("Bold",12)) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(0,LOCTEXT("Room","房间体块（点击已放房间编辑门窗）")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5)[ MakeRoomAppearancePanel() ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5)[ MakeRoofDressingPanel() ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(1,LOCTEXT("Pavilion","屋顶棚亭")) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(2,LOCTEXT("Dome","穹顶")) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(9,LOCTEXT("RoofCrown","斜顶墙冠")) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(3,LOCTEXT("Pots","瓦罐组合")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SComboButton).Visibility_Lambda([this]{return ActiveTool==3?EVisibility::Visible:EVisibility::Collapsed;})
                        .OnGetMenuContent(this,&SDesertBuildingDesigner::MakePotPlacementMenu)
                        .ButtonContent()[SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetPotPlacementText).AutoWrapText(true)] ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Visibility_Lambda([this]{return ActiveTool==3?EVisibility::Visible:EVisibility::Collapsed;})
                        .Text(LOCTEXT("PotPlacementHelp","整组移动；前后左右随朝向。点击已有瓦罐可改位置；自动靠墙或屋顶外缘，避开门与楼梯出口。独立三维窗只展示造型，主视窗预览才显示实际摆放。" )).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(4,LOCTEXT("Awning","篷布 + 支架")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SComboButton).Visibility_Lambda([this]{return ActiveTool==4 || ActiveTool==3?EVisibility::Visible:EVisibility::Collapsed;})
                        .OnGetMenuContent(this,&SDesertBuildingDesigner::MakeAwningVariantMenu)
                        .ButtonContent()[SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetAwningVariantText).AutoWrapText(true)] ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(5,LOCTEXT("Stairs","楼梯 + 平台")) ]
                    + SVerticalBox::Slot().AutoHeight()[ MakeToolButton(8,LOCTEXT("Rubble","外围散石组合（地面）")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)[ MakeMeshPicker(-1,LOCTEXT("CurrentMesh","当前组合块的模型（留空使用白模）")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(STextBlock).Text_Lambda([this]{ return GetMeshContract(ActiveTool); }).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetAssetCheckText).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.95f,0.75f,0.36f)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetFloorText).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SSpinBox<int32>).MinValue(0).MaxValue(6).MinSliderValue(0).MaxSliderValue(6)
                        .Value_Lambda([this]{ return EditFloor; })
                        .OnValueChanged_Lambda([this](int32 Value){ EditFloor=Value; bHoverNeedsRefresh=true; if (PreviewViewport) PreviewViewport->Invalidate(); }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6)
                    [ SNew(SButton).Text(this,&SDesertBuildingDesigner::GetFacingText).OnClicked_Lambda([this]
                        { SelectModulePreview(ActiveTool,StairLayout,ActiveTool==3?PotVariantIndex:AwningVariantIndex,(Facing+1)%4); return FReply::Handled(); }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SComboButton).Visibility_Lambda([this]{return ActiveTool==5?EVisibility::Visible:EVisibility::Collapsed;})
                        .OnGetMenuContent(this,&SDesertBuildingDesigner::MakeStairLayoutMenu)
                        .ButtonContent()[SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetStairLayoutText).AutoWrapText(true)] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5)
                    [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetLegacyStairText).AutoWrapText(true)
                        .Visibility_Lambda([this]{return UDesertBuildingEditorLibrary::GetLegacySwitchbackCount(PreviewBuilding)>0?EVisibility::Visible:EVisibility::Collapsed;}) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SButton).Text(LOCTEXT("MigrateOldStairs","显式转换旧折返为现代U形（可撤销）"))
                        .Visibility_Lambda([this]{return UDesertBuildingEditorLibrary::GetLegacySwitchbackCount(PreviewBuilding)>0?EVisibility::Visible:EVisibility::Collapsed;})
                        .OnClicked(this,&SDesertBuildingDesigner::MigrateLegacyStairs) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetInvalidDraftText).AutoWrapText(true)
                        .ColorAndOpacity(FLinearColor(1.f,0.42f,0.24f)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("ClearInvalid","清理旧的无效条目（可撤销）"))
                        .IsEnabled_Lambda([this]{return PreviewBuilding && (!PreviewBuilding->InvalidCellIndices.IsEmpty() || !PreviewBuilding->InvalidBlockIndices.IsEmpty() || !PreviewBuilding->InvalidRoomAppearanceIndices.IsEmpty());})
                        .OnClicked(this,&SDesertBuildingDesigner::ClearInvalidDraft) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)[ MakeMeshPicker(6,LOCTEXT("FoundationMesh","地基模型（自动生成，独立分组）")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6)[ MakeMeshPicker(7,LOCTEXT("ColumnMesh","支柱模型（规则自动生成）")) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(SExpandableArea).InitiallyCollapsed(true)
                        .HeaderContent()[ SNew(STextBlock).Text(LOCTEXT("Advanced","高级模块配套 / 材质 / 美术风格")) ]
                        .BodyContent()
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()[ AdvancedStyleDetails.ToSharedRef() ]
                        ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)
                    [ SNew(STextBlock).Text(LOCTEXT("ManagedSaveHint","新素材以你选择的文件夹为根：Buildings 保存建筑，Resources 分类收纳模型、材质和贴图；同根目录共享资源，旧素材更新保持原位。")).AutoWrapText(true) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)
                    [ SNew(SButton).Text(LOCTEXT("SaveNew","另存新素材 / 复制变体…")).OnClicked(this,&SDesertBuildingDesigner::SaveAs) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("SaveUpdate","保存更新（同步此素材的全部实例）"))
                        .IsEnabled_Lambda([this]{ return LoadedDesign!=nullptr; }).OnClicked(this,&SDesertBuildingDesigner::SaveUpdate) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("Apply","应用到场景中选中的建筑"))
                        .OnClicked(this,&SDesertBuildingDesigner::ApplySelected) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)
                    [ SNew(SButton).Text(LOCTEXT("Place","将当前已保存素材放入场景"))
                        .IsEnabled_Lambda([this]{ return LoadedDesign!=nullptr && !bDraftDirty; }).OnClicked(this,&SDesertBuildingDesigner::PlaceInLevel) ]
                ]
                ]
            ]
        ]
        + SSplitter::Slot().Value(0.71f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(STextBlock).Text(LOCTEXT("Instructions","悬浮：绿可放 / 红不可放；左键确认 · Shift + 左键或 Delete 删除当前类型\n按住右键 + WASD 移动 / QE 升降 / 鼠标转向 / 滚轮调速；Alt + 鼠标环绕；F 居中。")).AutoWrapText(true) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)[ SNew(SButton).Text(LOCTEXT("Undo","撤销")).OnClicked(this,&SDesertBuildingDesigner::Undo) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)[ SNew(SButton).Text(LOCTEXT("Redo","重做")).OnClicked(this,&SDesertBuildingDesigner::Redo) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)[ SNew(SButton).Text(LOCTEXT("Focus","居中预览")).OnClicked(this,&SDesertBuildingDesigner::FocusPreview) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0,8,5)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("CameraSpeed","相机速度（右键 + 滚轮也可调整）  ")) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SBox).WidthOverride(100)
                    [ SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(128.f).MinSliderValue(0.1f).MaxSliderValue(16.f)
                        .Value_Lambda([this]{return PreviewViewport ? PreviewViewport->GetViewportClient()->GetCameraSpeedSettings().GetCurrentSpeed() : 1.f;})
                        .OnValueChanged_Lambda([this](float Value)
                        { if(PreviewViewport) { FEditorViewportCameraSpeedSettings Speed=PreviewViewport->GetViewportClient()->GetCameraSpeedSettings(); Speed.SetCurrentSpeed(Value); PreviewViewport->GetViewportClient()->SetCameraSpeedSettings(Speed); } }) ] ] ]
            + SVerticalBox::Slot().FillHeight(1)
            [ SAssignNew(PreviewViewport,SDesertBuildingPreviewViewport).Scene(PreviewScene.Get()).Designer(SharedThis(this)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,5)
            [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetHoverText).AutoWrapText(true)
                .ColorAndOpacity_Lambda([this]{return GetHoverColor();}) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [ SNew(STextBlock).Text(this,&SDesertBuildingDesigner::GetStatusText).AutoWrapText(true) ]
        ]
    ];
    LastAction = TEXT("草稿准备好了。先在地面格添加房间；篷布放在旁边外围格，并选择朝外方向。");
    RefreshModulePreview();
    if(!DefaultStyle)
        LastAction+=TEXT("\n默认插件美术资源包尚未安装，当前只是临时白模。请先创建/安装插件默认资源包，或在左侧载入含持久材质的 Style，再保存建筑素材。");
}

SDesertBuildingDesigner::~SDesertBuildingDesigner()
{
    if (GEditor) GEditor->UnregisterForUndo(this);
    if (AdvancedStyleDetails) AdvancedStyleDetails->OnFinishedChangingProperties().Clear();
    ChildSlot[SNullWidget::NullWidget];
    AdvancedStyleDetails.Reset();
    PreviewViewport.Reset();
    ClearGhostGeometry();
    if (GhostRecipeBuilding) GhostRecipeBuilding->Destroy();
    ModulePreviewViewport.Reset(); ModuleToolThumbnails.Reset(); ModuleVariantThumbnails.Reset(); ModuleThumbnailPool.Reset();
    GhostRecipeBuilding=nullptr;
    GhostDynamicMaterial=nullptr;
    GhostMaterial=nullptr;
    if (PreviewBuilding) PreviewBuilding->Destroy();
    PreviewBuilding=nullptr;
    PreviewGround=nullptr;
    PreviewScene.Reset();
}

void SDesertBuildingDesigner::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewBuilding);
    Collector.AddReferencedObject(DraftStyle);
    Collector.AddReferencedObject(LoadedDesign);
    Collector.AddReferencedObject(PreviewGround);
    Collector.AddReferencedObject(GhostRecipeBuilding);
    Collector.AddReferencedObject(GhostMaterial);
    Collector.AddReferencedObject(GhostDynamicMaterial);
    Collector.AddReferencedObject(HoverCheck.CustomMesh);
    for (TObjectPtr<UInstancedStaticMeshComponent>& Component : GhostComponents) Collector.AddReferencedObject(Component);
    Collector.AddReferencedObject(DisplayedModuleData.SelectedMesh);
    for (FDesertModulePreviewPart& Part : DisplayedModuleData.Parts)
    {Collector.AddReferencedObject(Part.Mesh);for(TObjectPtr<UMaterialInterface>& Material:Part.Materials)Collector.AddReferencedObject(Material);}
}

void SDesertBuildingDesigner::PostUndo(bool bSuccess)
{
    if (bSuccess) { bDraftDirty=true; RebuildPreview(); LastAction=TEXT("已撤销/重做。预览按当前规则重新检查。"); }
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeToolButton(int32 Tool,const FText& Label)
{
    TSharedPtr<FAssetThumbnail> Thumbnail=MakeShared<FAssetThumbnail>(GetToolThumbnailMesh(Tool),64,64,ModuleThumbnailPool);
    ModuleToolThumbnails.Add(Tool,Thumbnail);
    return SNew(SButton).ContentPadding(FMargin(4,3))
        .ToolTipText(Tool==0 ? LOCTEXT("RoomThumbnailTip","整房槽留空时卡片是代表性墙片缩略图；上方独立三维展示才是当前单格组合。") : LOCTEXT("ModuleThumbnailTip","实际资源缩略图；上方三维展示会跟随当前Style材质与方向。"))
        .ButtonColorAndOpacity_Lambda([this,Tool]{ return ActiveTool==Tool ? FLinearColor(0.12f,0.46f,0.58f) : FLinearColor(0.2f,0.2f,0.2f); })
        .OnClicked_Lambda([this,Tool]{SelectModulePreview(Tool,StairLayout,Tool==3?PotVariantIndex:AwningVariantIndex,Facing);return FReply::Handled();})
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(64).HeightOverride(64)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().FillHeight(1)[SNew(SBox).Visibility_Lambda([this,Tool]{return GetToolThumbnailMesh(Tool)?EVisibility::Visible:EVisibility::Collapsed;})[Thumbnail->MakeThumbnailWidget()]]
                    + SVerticalBox::Slot().FillHeight(1)[SNew(STextBlock).Text(LOCTEXT("RecipeCard","配方 / 未配置"))
                        .AutoWrapText(true).Visibility_Lambda([this,Tool]{return GetToolThumbnailMesh(Tool)?EVisibility::Collapsed:EVisibility::Visible;})]]]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[SNew(STextBlock).Text(Label).AutoWrapText(true)]];
}

UStaticMesh* SDesertBuildingDesigner::GetToolThumbnailMesh(int32 Tool) const
{
    if(!DraftStyle) return nullptr;
    if(Tool==0 && !DraftStyle->RoomCellMesh) return DraftStyle->WallWindow ? DraftStyle->WallWindow.Get() : DraftStyle->WallSolid.Get();
    if(Tool==4 || Tool==3)
    {
        const FIntVector Context=HoveredCell.IsSet()?HoveredCell.GetValue():FIntVector(0,-1,0);
        return Tool==4 ? DraftStyle->ResolveAwningBayMesh(AwningVariantIndex,Context,PreviewBuilding ? PreviewBuilding->Seed : 17) :
            DraftStyle->ResolvePotClusterMesh(PotVariantIndex,Context,PreviewBuilding ? PreviewBuilding->Seed : 17);
    }
    return GetSlotMesh(Tool);
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeModulePreviewPanel()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("ModelPanel","当前模块 · 独立三维展示")).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))]
        + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(220)[SAssignNew(ModulePreviewViewport,SDesertModulePreviewViewport)]]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(DisplayedModuleData.Description);}).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("MiniControls","拖动鼠标环绕 · 滚轮缩放 · F居中。资源图显示原模型，三维展示使用当前Style材质；是否可放请看右侧红/绿格预览。")).AutoWrapText(true)];
}

void SDesertBuildingDesigner::RefreshModulePreview()
{
    if(!PreviewBuilding || !ModulePreviewViewport) return;
    const FIntVector Context=ActiveTool==0 ? (SelectedRoomCell.IsSet()?SelectedRoomCell.GetValue():FIntVector::ZeroValue) :
        ((ActiveTool==4 || ActiveTool==3) && HoveredCell.IsSet()?HoveredCell.GetValue():FIntVector(0,-1,0));
    const int32 Variant=DraftStyle ? (ActiveTool==4 ? DraftStyle->ResolveAwningBayVariantIndex(AwningVariantIndex,Context,PreviewBuilding->Seed) :
        (ActiveTool==3?DraftStyle->ResolvePotClusterVariantIndex(PotVariantIndex,Context,PreviewBuilding->Seed):INDEX_NONE)):INDEX_NONE;
    // 房间外观依赖选中格；自动棚只依赖最终变体。其他模块不会因鼠标移过格子重建世界。
    const bool bRoomContextChanged=ActiveTool==0 && Context!=LastModuleContextCell;
    if(!bModulePreviewNeedsRefresh && !bRoomContextChanged && Variant==LastModuleResolvedVariant) return;
    bModulePreviewNeedsRefresh=false; LastModuleContextCell=Context;LastModuleResolvedVariant=Variant;
    DisplayedModuleData=DesertMakeModulePreviewData(PreviewBuilding,ActiveTool,StairLayout,ActiveTool==3?PotVariantIndex:AwningVariantIndex,Facing,Context);
    ModulePreviewViewport->SetPreviewData(DisplayedModuleData);
    for(auto& Entry:ModuleToolThumbnails) Entry.Value->SetAsset(GetToolThumbnailMesh(Entry.Key));
}

bool SDesertBuildingDesigner::SelectModulePreview(int32 Tool,EDesertStairLayout Layout,int32 VariantIndex,int32 InFacing)
{
    if(!(Tool==0 || Tool==1 || Tool==2 || Tool==3 || Tool==4 || Tool==5 || Tool==8 || Tool==9) || InFacing<0 || InFacing>3 || VariantIndex < -1 ||
        !DesertGetAuthoringStairLayouts().Contains(Layout)) return false;
    if (Tool!=3) SelectedPotCell.Reset();
    ActiveTool=Tool; StairLayout=Layout;if(Tool==3) PotVariantIndex=VariantIndex;else if(Tool==4) AwningVariantIndex=VariantIndex;Facing=InFacing;
    bModulePreviewNeedsRefresh=true;bHoverNeedsRefresh=true;RefreshModulePreview();RefreshHoverPreview();
    return true; // 只改变工具和独立展示，未更改Cells/Blocks/Style和脏标记。
}

bool SDesertBuildingDesigner::LoadDesignSafely(UDesertBuildingDesign* Design,FString& Message)
{
    if(!Design || !PreviewBuilding) {Message=TEXT("没有有效建筑素材或预览尚未就绪");return false;}
    if(bDraftDirty) {Message=TEXT("设计器已有未保存草稿；请先保存/另存或明确新建草稿。没有覆盖当前内容。");return false;}
    LoadDesign(Design); Message=TEXT("已在真实设计器载入素材；模块卡和独立3D已更新，未修改原素材");return true;
}

FText SDesertBuildingDesigner::GetLegacyStairText() const
{
    return FText::FromString(FString::Printf(TEXT("草稿保留%d个旧折返条目。旧枚举和数据仍可读取；新放置菜单已改用现代U形。点击下方按钮才会在规则通过后转换，失败条目保留。"),UDesertBuildingEditorLibrary::GetLegacySwitchbackCount(PreviewBuilding)));
}
FReply SDesertBuildingDesigner::MigrateLegacyStairs()
{
    const int32 Converted=UDesertBuildingEditorLibrary::MigrateLegacySwitchbackStairs(PreviewBuilding);
    LastAction=UDesertBuildingEditorLibrary::GetLastStairMigrationMessage();
    if(Converted>0) {bDraftDirty=true;RebuildPreview();}
    return FReply::Handled();
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeMeshPicker(int32 Slot,const FText& Label)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(Label).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SObjectPropertyEntryBox).AllowedClass(UStaticMesh::StaticClass()).AllowClear(true)
            .IsEnabled_Lambda([this,Slot]{const int32 Actual=Slot<0?ActiveTool:Slot;return !DraftStyle ||
                (Actual==4?AwningVariantIndex>=0 || DraftStyle->ModuleAwningBayVariants.IsEmpty():
                 Actual==3?PotVariantIndex>=0 || DraftStyle->ModulePotClusterVariants.IsEmpty():true);})
            .ObjectPath_Lambda([this,Slot]{ UStaticMesh* Mesh=GetSlotMesh(Slot<0?ActiveTool:Slot); return Mesh?Mesh->GetPathName():FString(); })
            .OnObjectChanged_Lambda([this,Slot](const FAssetData& Asset){ SetSlotMesh(Slot<0?ActiveTool:Slot,Cast<UStaticMesh>(Asset.GetAsset())); }) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(STextBlock).Text_Lambda([this,Slot]{ return Slot<0?FText::GetEmpty():GetMeshContract(Slot); }).AutoWrapText(true) ];
}

UStaticMesh* SDesertBuildingDesigner::GetSlotMesh(int32 Slot) const
{
    if (!DraftStyle) return nullptr;
    switch (Slot)
    {
        case 0:return DraftStyle->RoomCellMesh;
        case 1:return DraftStyle->ModuleRoofPavilion;
        case 2:return DraftStyle->ModuleDome;
        case 3:
            if(DraftStyle->ModulePotClusterVariants.IsEmpty()) return PotVariantIndex==-1?DraftStyle->ModulePotCluster.Get():nullptr;
            if(PotVariantIndex>=0) return DraftStyle->ModulePotClusterVariants.IsValidIndex(PotVariantIndex)?DraftStyle->ModulePotClusterVariants[PotVariantIndex].Get():nullptr;
            return DraftStyle->ResolvePotClusterMesh(-1,HoveredCell.IsSet()?HoveredCell.GetValue():FIntVector(0,-1,0),PreviewBuilding?PreviewBuilding->Seed:17);
        case 4:
            if(DraftStyle->ModuleAwningBayVariants.IsEmpty()) return AwningVariantIndex==-1?DraftStyle->ModuleAwningBay.Get():nullptr;
            if(AwningVariantIndex>=0) return DraftStyle->ModuleAwningBayVariants.IsValidIndex(AwningVariantIndex)?DraftStyle->ModuleAwningBayVariants[AwningVariantIndex].Get():nullptr;
            return HoverCheck.ModuleKind==EDesertModuleKind::AwningBay ? HoverCheck.CustomMesh.Get() : nullptr;
        case 5:return StairLayout==EDesertStairLayout::LShape ? DraftStyle->ModuleLShapeStairs.Get() :
            (StairLayout==EDesertStairLayout::UShape ? DraftStyle->ModuleUShapeStairs.Get() :
            (StairLayout==EDesertStairLayout::Switchback ? DraftStyle->ModuleSwitchbackStairs.Get() :
            (StairLayout==EDesertStairLayout::OutwardLegacy ? DraftStyle->ModuleStairs.Get() : DraftStyle->ModuleWallStairs.Get())));
        case 6:return DraftStyle->Foundation;
        case 7:return DraftStyle->ModuleColumn;
        case 8:return DraftStyle->ModuleRubbleCluster;
        case 9:return DraftStyle->ModuleRoofCrown;
        default:return nullptr;
    }
}

void SDesertBuildingDesigner::SetSlotMesh(int32 Slot,UStaticMesh* Mesh)
{
    if (!DraftStyle) return;
    if(Slot==3 && PotVariantIndex>=0 && !DraftStyle->ModulePotClusterVariants.IsValidIndex(PotVariantIndex))
    {LastAction=TEXT("瓦罐组变体下标已不存在；先在高级Style中配置数组。");return;}
    if(Slot==3 && PotVariantIndex==-1 && !DraftStyle->ModulePotClusterVariants.IsEmpty())
    {LastAction=TEXT("自动瓦罐组选择需先选明确变体，才能替换该项资源。");return;}
    if(Slot==4 && AwningVariantIndex>=0 && !DraftStyle->ModuleAwningBayVariants.IsValidIndex(AwningVariantIndex))
    {LastAction=TEXT("此棚架变体下标已不存在；请重新选择变体，或在高级Style中添加该项。");return;}
    if(Slot==4 && AwningVariantIndex==-1 && !DraftStyle->ModuleAwningBayVariants.IsEmpty())
    {LastAction=TEXT("自动模式由格坐标和Seed选择整组合；请选择一个明确变体后再替换它的模型。");return;}
    FScopedTransaction Transaction(LOCTEXT("ChooseMesh","修改建筑草稿的美术模型"));
    DraftStyle->Modify();
    switch (Slot)
    {
        case 0:DraftStyle->RoomCellMesh=Mesh;break;
        case 1:DraftStyle->ModuleRoofPavilion=Mesh;break;
        case 2:DraftStyle->ModuleDome=Mesh;break;
        case 3:if(PotVariantIndex>=0) DraftStyle->ModulePotClusterVariants[PotVariantIndex]=Mesh;else DraftStyle->ModulePotCluster=Mesh;break;
        case 4:
            if(AwningVariantIndex>=0) DraftStyle->ModuleAwningBayVariants[AwningVariantIndex]=Mesh;
            else DraftStyle->ModuleAwningBay=Mesh;
            break;
        case 5:if(StairLayout==EDesertStairLayout::LShape) DraftStyle->ModuleLShapeStairs=Mesh;
            else if(StairLayout==EDesertStairLayout::UShape) DraftStyle->ModuleUShapeStairs=Mesh;
            else if(StairLayout==EDesertStairLayout::Switchback) DraftStyle->ModuleSwitchbackStairs=Mesh;
            else if(StairLayout==EDesertStairLayout::OutwardLegacy) DraftStyle->ModuleStairs=Mesh;
            else DraftStyle->ModuleWallStairs=Mesh;break;
        case 6:DraftStyle->Foundation=Mesh;break;
        case 7:DraftStyle->ModuleColumn=Mesh;break;
        case 8:DraftStyle->ModuleRubbleCluster=Mesh;break;
        case 9:DraftStyle->ModuleRoofCrown=Mesh;break;
    }
    bDraftDirty=true;
    LastAction=TEXT("模型已融入预览。该槽位的所有对应组合块会使用所选模型。");
    RebuildPreview();
}

bool SDesertBuildingDesigner::HasPlacedPot(FIntVector Cell) const
{
    return PreviewBuilding && PreviewBuilding->Blocks.ContainsByPredicate([Cell](const FDesertBlockPlacement& Entry)
    {return Entry.bEnabled && Entry.Type==EDesertBlockType::PotCluster && Entry.Cell==Cell;});
}

FText SDesertBuildingDesigner::GetPotPlacementText() const
{
    const FString Label=PotPlacement==EDesertPotPlacement::LegacyCentered ? TEXT("旧素材居中（选择新位置可升级）") :
        StaticEnum<EDesertPotPlacement>()->GetDisplayNameTextByValue(static_cast<int64>(PotPlacement)).ToString();
    return FText::FromString(TEXT("瓦罐摆放位置：")+Label+(SelectedPotCell.IsSet()?TEXT(" · 修改已选组"):TEXT(" · 新放置")));
}

bool SDesertBuildingDesigner::SetPotPlacementSelection(EDesertPotPlacement Position)
{
    if (Position<EDesertPotPlacement::Automatic || Position>EDesertPotPlacement::Center) return false;
    if (PreviewBuilding && SelectedPotCell.IsSet())
    {
        const int32 Index=PreviewBuilding->Blocks.IndexOfByPredicate([this](const FDesertBlockPlacement& Entry)
        {return Entry.bEnabled && Entry.Type==EDesertBlockType::PotCluster && Entry.Cell==SelectedPotCell.GetValue();});
        if (Index!=INDEX_NONE)
        {
            FDesertBlockPlacement Proposed=PreviewBuilding->Blocks[Index]; Proposed.PotPlacement=Position;
            TArray<FDesertBlockPlacement> Others=PreviewBuilding->Blocks; Others.RemoveAt(Index);
            FDesertPlacementCheck Check;
            {TGuardValue<TArray<FDesertBlockPlacement>> Scope(PreviewBuilding->Blocks,Others); Check=PreviewBuilding->EvaluatePlacement(false,Proposed);}
            if (!Check.bAllowed) {LastAction=TEXT("未修改瓦罐：")+Check.Reason;return false;}
            FScopedTransaction Transaction(LOCTEXT("MovePotGroup","调整瓦罐整组摆放位置"));
            PreviewBuilding->Modify(); PreviewBuilding->Blocks[Index]=Proposed; bDraftDirty=true;
            LastAction=TEXT("已调整选中瓦罐整组的位置；保存命名素材后同步关联实例，可撤销。");
        }
        else SelectedPotCell.Reset();
    }
    PotPlacement=Position; bHoverNeedsRefresh=true;
    if (SelectedPotCell.IsSet()) RebuildPreview(); else RefreshHoverPreview();
    if (PreviewViewport) PreviewViewport->Invalidate();
    return true;
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakePotPlacementMenu()
{
    FMenuBuilder Menu(true,nullptr);
    for (uint8 Value=static_cast<uint8>(EDesertPotPlacement::Automatic);Value<=static_cast<uint8>(EDesertPotPlacement::Center);++Value)
    {
        const EDesertPotPlacement Position=static_cast<EDesertPotPlacement>(Value);
        Menu.AddMenuEntry(StaticEnum<EDesertPotPlacement>()->GetDisplayNameTextByValue(Value),
            LOCTEXT("PotPlaceTip","整组移动，方向相对朝向；主视窗检查门口、出口和空间。"),FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this,Position]{SetPotPlacementSelection(Position);}),FCanExecuteAction(),
                FIsActionChecked::CreateLambda([this,Position]{return PotPlacement==Position;})),NAME_None,EUserInterfaceActionType::RadioButton);
    }
    return Menu.MakeWidget();
}

FText SDesertBuildingDesigner::GetAwningVariantText() const
{
    if(ActiveTool==3)
    {
        if(PotVariantIndex<0) return LOCTEXT("PotAuto","瓦罐整组：自动（格坐标 + Seed）");
        UStaticMesh* Mesh=DraftStyle && DraftStyle->ModulePotClusterVariants.IsValidIndex(PotVariantIndex)?DraftStyle->ModulePotClusterVariants[PotVariantIndex].Get():nullptr;
        return FText::FromString(FString::Printf(TEXT("瓦罐整组 %d：%s"),PotVariantIndex+1,Mesh?*Mesh->GetName():TEXT("未配置/下标失效")));
    }
    if(AwningVariantIndex<0)
        return LOCTEXT("AwningAuto","棚架变体：自动（按格坐标与Seed稳定选择）");
    UStaticMesh* Mesh=DraftStyle && DraftStyle->ModuleAwningBayVariants.IsValidIndex(AwningVariantIndex)
        ? DraftStyle->ModuleAwningBayVariants[AwningVariantIndex].Get() : nullptr;
    return FText::FromString(FString::Printf(TEXT("棚架变体 %d：%s"),AwningVariantIndex+1,
        Mesh?*Mesh->GetName():TEXT("未配置/下标已失效")));
}

FText SDesertBuildingDesigner::GetStairLayoutText() const
{
    const TCHAR* Names[]={TEXT("旧朝外直梯"),TEXT("贴墙直梯 + 两端平台"),TEXT("旧折返梯"),TEXT("L形转角双跑 + 中平台"),TEXT("U形折返双跑 + 中平台")};
    return FText::FromString(FString(TEXT("梯型："))+Names[FMath::Clamp(static_cast<int32>(StairLayout),0,4)]);
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeStairLayoutMenu()
{
    FMenuBuilder Menu(true,nullptr);
    for(EDesertStairLayout Layout:DesertGetAuthoringStairLayouts())
    {
        const TCHAR* Name=Layout==EDesertStairLayout::AlongWall ? TEXT("贴墙直梯 + 平台") :
            (Layout==EDesertStairLayout::LShape?TEXT("L形转角双跑 + 实体中平台") :
            (Layout==EDesertStairLayout::UShape?TEXT("U形折返双跑 + 实体平台"):TEXT("朝外直梯")));
        Menu.AddMenuEntry(FText::FromString(Name),LOCTEXT("StairPickHelp","切换同步独立3D和待放预览；固定模型仍检查实际级数、净宽和踏深。旧折返只兼容读取，新增用现代U形。"),FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this,Layout]{SelectModulePreview(5,Layout,AwningVariantIndex,Facing);}),
            FCanExecuteAction(),FIsActionChecked::CreateLambda([this,Layout]{return StairLayout==Layout;})),NAME_None,EUserInterfaceActionType::RadioButton);
    }
    return Menu.MakeWidget();
}

FReply SDesertBuildingDesigner::ApplySelectedRoomAppearance(EDesertRoomDoorMode Mode,bool bChangeDoor,int32 WindowSide,bool bToggleWindows)
{
    if (!PreviewBuilding || !SelectedRoomCell.IsSet() || (GEditor && GEditor->PlayWorld)) return FReply::Handled();
    FDesertRoomAppearance Appearance=PreviewBuilding->GetRoomAppearance(SelectedRoomCell.GetValue());
    if (bChangeDoor) Appearance.DoorMode=Mode;
    if (bToggleWindows) Appearance.bOverrideWindows=!Appearance.bOverrideWindows;
    if (WindowSide>=0 && WindowSide<4) Appearance.WindowMask^=1<<WindowSide;
    FScopedTransaction Transaction(LOCTEXT("EditRoomAppearance","编辑选中房间门窗"));
    if (PreviewBuilding->SetRoomAppearance(Appearance))
    { bDraftDirty=true; LastAction=PreviewBuilding->LastRoomAppearanceMessage; RebuildPreview(); }
    else LastAction=TEXT("门窗未修改：")+PreviewBuilding->LastRoomAppearanceMessage;
    return FReply::Handled();
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeRoomAppearancePanel()
{
    TSharedRef<SVerticalBox> Panel=SNew(SVerticalBox);
    Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]
    { return FText::FromString(SelectedRoomCell.IsSet() ? FString::Printf(TEXT("已选房间 (%d,%d,%d) · 门与窗"),SelectedRoomCell->X,SelectedRoomCell->Y,SelectedRoomCell->Z) : TEXT("点击预览中已有房间，直接编辑门窗")); }).AutoWrapText(true)];
    const TCHAR* DoorLabels[]={TEXT("继承自动"),TEXT("无门"),TEXT("前 -Y"),TEXT("右 +X"),TEXT("后 +Y"),TEXT("左 -X")};
    for (int32 Row=0;Row<2;++Row)
    {
        TSharedRef<SHorizontalBox> Buttons=SNew(SHorizontalBox);
        for (int32 Col=0;Col<3;++Col)
        {
            const EDesertRoomDoorMode Mode=static_cast<EDesertRoomDoorMode>(Row*3+Col);
            Buttons->AddSlot().FillWidth(1).Padding(1)[SNew(SButton).Text(FText::FromString(DoorLabels[Row*3+Col]))
                .IsEnabled_Lambda([this]{return SelectedRoomCell.IsSet() && PreviewBuilding;})
                .ButtonColorAndOpacity_Lambda([this,Mode]
                {return PreviewBuilding && SelectedRoomCell.IsSet() && PreviewBuilding->GetRoomAppearance(SelectedRoomCell.GetValue()).DoorMode==Mode ? FLinearColor(.65f,.42f,.12f) : FLinearColor::White;})
                .OnClicked_Lambda([this,Mode]{return ApplySelectedRoomAppearance(Mode,true);})];
        }
        Panel->AddSlot().AutoHeight()[Buttons];
    }
    Panel->AddSlot().AutoHeight()[SNew(SCheckBox).IsEnabled_Lambda([this]{return SelectedRoomCell.IsSet() && PreviewBuilding;})
        .IsChecked_Lambda([this]{return PreviewBuilding && SelectedRoomCell.IsSet() && PreviewBuilding->GetRoomAppearance(SelectedRoomCell.GetValue()).bOverrideWindows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([this](ECheckBoxState){ApplySelectedRoomAppearance(EDesertRoomDoorMode::Automatic,false,-1,true);})
        [SNew(STextBlock).Text(LOCTEXT("ExplicitWindows","手动设置四面窗（关闭后继承Seed自动窗）")).AutoWrapText(true)]];
    TSharedRef<SHorizontalBox> Windows=SNew(SHorizontalBox);
    const TCHAR* Sides[]={TEXT("前"),TEXT("右"),TEXT("后"),TEXT("左")};
    for (int32 Side=0;Side<4;++Side)
        Windows->AddSlot().FillWidth(1)[SNew(SCheckBox)
            .IsEnabled_Lambda([this]{return PreviewBuilding && SelectedRoomCell.IsSet() && PreviewBuilding->GetRoomAppearance(SelectedRoomCell.GetValue()).bOverrideWindows;})
            .IsChecked_Lambda([this,Side]{return PreviewBuilding && SelectedRoomCell.IsSet() && (PreviewBuilding->GetRoomAppearance(SelectedRoomCell.GetValue()).WindowMask&(1<<Side)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Side](ECheckBoxState){ApplySelectedRoomAppearance(EDesertRoomDoorMode::Automatic,false,Side);})[SNew(STextBlock).Text(FText::FromString(Sides[Side]))]];
    Panel->AddSlot().AutoHeight()[Windows];
    Panel->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("SharedFaceHint","相邻房间共享面自动隐藏并内部连通；门面优先于同面窗。朝内部门会明确拒绝，原配置不变。")).AutoWrapText(true)];
    return SNew(SBorder).Padding(5).Visibility_Lambda([this]{return ActiveTool==0?EVisibility::Visible:EVisibility::Collapsed;})[Panel];
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeAwningVariantMenu()
{
    FMenuBuilder Menu(true,nullptr);
    ModuleVariantThumbnails.Reset();
    auto AddChoice=[this,&Menu](int32 Index,const FText& Label)
    {
        const FIntVector Context=HoveredCell.IsSet()?HoveredCell.GetValue():FIntVector(0,-1,0);
        const int32 Tool=ActiveTool==3?3:4;
        UStaticMesh* Mesh=DraftStyle ? (Tool==3?DraftStyle->ResolvePotClusterMesh(Index,Context,PreviewBuilding?PreviewBuilding->Seed:17):
            DraftStyle->ResolveAwningBayMesh(Index,Context,PreviewBuilding?PreviewBuilding->Seed:17)) : nullptr;
        TSharedPtr<FAssetThumbnail> Thumbnail=MakeShared<FAssetThumbnail>(Mesh,80,80,ModuleThumbnailPool);
        ModuleVariantThumbnails.Add(Thumbnail);
        Menu.AddWidget(SNew(SButton).ContentPadding(4)
            .ButtonColorAndOpacity(Index==(Tool==3?PotVariantIndex:AwningVariantIndex)?FLinearColor(0.12f,0.46f,0.58f):FLinearColor(0.2f,0.2f,0.2f))
            .ToolTipText(LOCTEXT("AwningVariantTip","选择只影响下一次点击。自动按格坐标与建筑Seed选择整组合；空槽或越界不会回退其他模型。"))
            .OnClicked_Lambda([this,Index,Tool]{SelectModulePreview(Tool,StairLayout,Index,Facing);FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(80).HeightOverride(80)
                    [Mesh ? Thumbnail->MakeThumbnailWidget() : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("EmptyVariantCard","未配置模型")).AutoWrapText(true))]]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[SNew(STextBlock).Text(Label).AutoWrapText(true)]],
            FText::GetEmpty(),true);
    };
    AddChoice(-1,LOCTEXT("AwningAutoMenu","自动（格坐标 + Seed）"));
    if(DraftStyle)
        for(int32 Index=0;Index<(ActiveTool==3?DraftStyle->ModulePotClusterVariants.Num():DraftStyle->ModuleAwningBayVariants.Num());++Index)
        {
            UStaticMesh* Mesh=ActiveTool==3?DraftStyle->ModulePotClusterVariants[Index].Get():DraftStyle->ModuleAwningBayVariants[Index].Get();
            AddChoice(Index,FText::FromString(FString::Printf(TEXT("变体 %d · %s"),Index+1,
                Mesh?*Mesh->GetName():TEXT("未配置模型：选中后可填入资源"))));
        }
    return Menu.MakeWidget();
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeRoofDressingPanel()
{
    return SNew(SBorder).Padding(5).Visibility_Lambda([this]{return ActiveTool==0?EVisibility::Visible:EVisibility::Collapsed;})
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox)
            .IsChecked_Lambda([this]{return DraftStyle && DraftStyle->bEnableRoofDressing?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){if(DraftStyle){FScopedTransaction Tx(LOCTEXT("RoofDressingToggle","改变屋顶装饰"));DraftStyle->Modify();DraftStyle->bEnableRoofDressing=State==ECheckBoxState::Checked;bDraftDirty=true;RebuildPreview();}})
            [SNew(STextBlock).Text(LOCTEXT("RoofDressingEnabled","屋顶装饰（关闭 = 原样屋顶）")).AutoWrapText(true)]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox)
            .IsEnabled_Lambda([this]{return DraftStyle && DraftStyle->bEnableRoofDressing;})
            .IsChecked_Lambda([this]{return DraftStyle && DraftStyle->bRandomizeRoofDressing?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State){if(DraftStyle){FScopedTransaction Tx(LOCTEXT("RoofRandomToggle","改变屋顶装饰随机模式"));DraftStyle->Modify();DraftStyle->bRandomizeRoofDressing=State==ECheckBoxState::Checked;bDraftDirty=true;RebuildPreview();}})
            [SNew(STextBlock).Text(LOCTEXT("RoofDressingRandom","按格稳定随机（Seed）"))]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SComboButton)
            .IsEnabled_Lambda([this]{return DraftStyle && DraftStyle->bEnableRoofDressing && !DraftStyle->bRandomizeRoofDressing;})
            .OnGetMenuContent(this,&SDesertBuildingDesigner::MakeRoofDressingMenu)
            .ButtonContent()[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("固定屋顶装饰：%d（见资源图卡）"),DraftStyle?DraftStyle->RoofDressingVariantIndex+1:1));}).AutoWrapText(true)]]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("RoofDressingSafe","只装饰无屋顶组合块、无开口的外露顶格；有楼梯出口时该格保留原屋顶。资源替换在高级Style > Roof Dressing。")).AutoWrapText(true)]];
}

TSharedRef<SWidget> SDesertBuildingDesigner::MakeRoofDressingMenu()
{
    FMenuBuilder Menu(true,nullptr);ModuleVariantThumbnails.Reset();
    if(DraftStyle)
        for(int32 Index=0;Index<DraftStyle->RoofDressingVariants.Num();++Index)
        {
            UStaticMesh* Mesh=DraftStyle->RoofDressingVariants[Index].Get();
            TSharedPtr<FAssetThumbnail> Thumbnail=MakeShared<FAssetThumbnail>(Mesh,80,80,ModuleThumbnailPool);ModuleVariantThumbnails.Add(Thumbnail);
            Menu.AddWidget(SNew(SButton).ContentPadding(4)
                .OnClicked_Lambda([this,Index]{if(DraftStyle){FScopedTransaction Tx(LOCTEXT("ChooseRoofDressing","选择屋顶装饰整组"));DraftStyle->Modify();DraftStyle->RoofDressingVariantIndex=Index;bDraftDirty=true;RebuildPreview();}FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(80).HeightOverride(80)[Mesh?Thumbnail->MakeThumbnailWidget():StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("EmptyRoofCard","未配置模型")))]]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d · %s"),Index+1,Mesh?*Mesh->GetName():TEXT("空项")))).AutoWrapText(true)]],FText::GetEmpty(),true);
        }
    if(!DraftStyle || DraftStyle->RoofDressingVariants.IsEmpty()) Menu.AddWidget(SNew(STextBlock).Text(LOCTEXT("NoRoofAssets","未配置屋顶装饰数组，请在高级Style填入模型。")),FText::GetEmpty(),true);
    return Menu.MakeWidget();
}

FText SDesertBuildingDesigner::GetMeshContract(int32 Slot) const
{
    switch(Slot)
    {
        case 0:return LOCTEXT("RoomContract","房间格：300 × 300 × 300 cm。正式模块套件应把整房模型槽留空，由Style中的墙、门窗、屋顶按相邻格自动组装；这是正常配置。若选入整房模型，则按其包围盒适配格子并校正底面中心、正门朝-Y；门窗/屋顶和邻接内部面由整房模型作者提供，不能自动切开其屋顶入口。");
        case 1:return LOCTEXT("PavilionContract","棚亭：280 × 280 × 220 cm；底面中心，包含支柱和顶棚。只能放在外露屋顶。");
        case 2:return LOCTEXT("DomeContract","穹顶：280 × 280 × 180 cm；底面中心。放在屋顶或棚亭顶部。");
        case 3:return LOCTEXT("PotContract","瓦罐组：140 × 110 × 85 cm；底面中心。只能放屋顶或房屋外围地面。");
        case 4:return LOCTEXT("AwningContract","篷布支架名义基准：280 × 200 × 240 cm。变体是完整布、梁、杆、柱脚；不强拉各模型包围盒到同一大小。前-Y，实际后缘贴墙，实际最低点贴地，需恰好一面邻墙。整组包围盒四角地面高差≤10cm；不逐根柱脚独立贴坡。自动模式中先选明确变体才可替换该项模型。");
        case 5:
            if(StairLayout==EDesertStairLayout::LShape) return LOCTEXT("LContract","L转角双跑：510×510×300cm，16级、净120、踏深30；出口原点+Y入屋。左前跑与右后跑分段占位，内空矩形不占房格。固定模型仅一层，真实级高≤22cm。");
            if(StairLayout==EDesertStairLayout::UShape) return LOCTEXT("UContract","U折返双跑：300×480×300cm，16级、净120、踏深30；左近墙下入口，右近墙上出口，中平台120深。固定模型仅一层，真实级高≤22cm。");
            return StairLayout==EDesertStairLayout::Switchback
            ? LOCTEXT("SwitchbackContract","折返梯：300 × 400 × 300 cm；原点为出口地面投影，+Y 入屋；包围盒 X[-225,75]、Y[-400,0]，出口(0,0,300)。")
            : LOCTEXT("StraightContract","贴墙直梯：600 × 120 × 300 cm；原点为出口地面投影，+Y 入屋；包围盒 X[-540,60]、Y[-120,0]，出口(0,0,300)。");
        case 6:return LOCTEXT("FoundationContract","地基：300 × 300 × 100 cm；原点在上表面中心，向 -Z 延伸；地基始终为独立分组。");
        case 7:return LOCTEXT("ColumnContract","支柱：60 × 60 × 300 cm；底面中心，包含柱脚和柱头；悬空房间按高度自动拉伸。");
        case 8:return LOCTEXT("RubbleContract","外围散石：220 × 160 × 65 cm；底面中心。整组手工点击放置，仅首层外墙外围地面，避开门/楼梯；默认不阻挡玩家，归地基环境组，不随楼层淡化。不是自动散布。");
        case 9:return LOCTEXT("RoofCrownContract","斜顶墙冠：名义300 × 300 × 180 cm，底面中心Z=0；是永久墙壳，不是篷布亭。只能放有效外露屋顶，实际模型参与相交检查并归对应楼层淡化。未配置模型会拒绝放置，不能直接放地面。");
        default:return FText::GetEmpty();
    }
}

FText SDesertBuildingDesigner::GetAssetCheckText() const
{
    UStaticMesh* Mesh=GetSlotMesh(ActiveTool);
    if(ActiveTool==9 && !Mesh)
        return LOCTEXT("RoofCrownMissing","Style尚未配置斜顶墙冠模型；当前点击会被拒绝，没有隐藏条目，也不会换成穹顶/布亭。");
    if(ActiveTool==4 && DraftStyle && (AwningVariantIndex>=0 || !DraftStyle->ModuleAwningBayVariants.IsEmpty()) && !Mesh)
        return FText::FromString(AwningVariantIndex==-1
            ? TEXT("自动模式：将鼠标移到格子，预览会显示按坐标和Seed选择的整套棚架。空变体槽会拒绝放置，不会悄悄换模型。要替换资源，先在下拉框选择明确变体。")
            : TEXT("当前棚架变体未配置模型或下标已失效；不会使用旧棚架模型替代。请填写当前槽或选择另一变体。"));
    if (!Mesh && ActiveTool==0 && DraftStyle)
    {
        const bool bAnyWall=DraftStyle->WallSolid || DraftStyle->WallWindow || DraftStyle->WallDoor;
        if (DraftStyle->WallSolid && DraftStyle->WallWindow && DraftStyle->WallDoor)
            return LOCTEXT("WallKitConfigured","当前房间由Style中的正式墙片、门窗和屋顶生成；整房槽为空是正常的，不代表使用白模。端版或其他类型缺失时会给出规则警告并回退对应白模，请检查预览和状态提示。");
        if (bAnyWall)
            return LOCTEXT("WallKitPartial","整房槽为空，当前已配置部分正式墙片；未填写的墙/门窗类型仍会使用白模。请在美术风格配置中补齐所需槽位。");
    }
    if (!Mesh) return LOCTEXT("NoMesh","当前槽位使用程序白模。选入正式模型后会保留模型的材质槽。");
    const FBox Box=Mesh->GetBoundingBox();
    const FVector Size=Box.GetSize();
    const UBodySetup* Body=Mesh->GetBodySetup();
    FString Notes=FString::Printf(TEXT("模型实测：%.0f × %.0f × %.0f cm；Z 最低 %.1f cm。"),Size.X,Size.Y,Size.Z,Box.Min.Z);
    if (ActiveTool!=8 && (!Body || (Body->AggGeom.GetElementCount()==0 && Body->CollisionTraceFlag!=CTF_UseComplexAsSimple)))
        Notes+=TEXT("\n缺少可用简单碰撞；请在静态网格编辑器添加碰撞后用于游戏。");
    if (ActiveTool==8) Notes+=TEXT("\n外围小散石默认无Pawn碰撞，未制作碰撞体不会封住玩家。归地基环境组，保持显示。");
    bool bAllFade=true;
    for(const FStaticMaterial& Material:Mesh->GetStaticMaterials())
    {
        float Value=0;
        if(!Material.MaterialInterface || !Material.MaterialInterface->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OcclusionFade")),Value)) bAllFade=false;
    }
    if(!bAllFade && ActiveTool!=8) Notes+=TEXT("\n部分材质未暴露 OcclusionFade 参数，需配置材质后才会产生可见透视效果。");
    if(ActiveTool!=5 && !(ActiveTool==0 && DraftStyle->bFitRoomCellMeshToGrid)
        && (FMath::Abs(Box.GetCenter().X)>5 || FMath::Abs(Box.GetCenter().Y)>5 || FMath::Abs(Box.Min.Z)>5))
        Notes+=TEXT("\n原点可能不符合底面中心约定；请核对原点，预览中的偏移会在成品中保留。");
    return FText::FromString(Notes);
}

FText SDesertBuildingDesigner::GetFloorText() const
{
    return FText::FromString(EditFloor==0 ? TEXT("编辑平面：地面 / 第一层房间底面")
        : FString::Printf(TEXT("编辑平面：第 %d 层屋顶 / 第 %d 层房间底面"),EditFloor,EditFloor+1));
}

FText SDesertBuildingDesigner::GetFacingText() const
{
    const TCHAR* Names[]={TEXT("前侧 -Y"),TEXT("右侧 +X"),TEXT("后侧 +Y"),TEXT("左侧 -X")};
    return FText::FromString(FString::Printf(TEXT("朝外方向：%s（点击旋转）"),Names[Facing]));
}

FText SDesertBuildingDesigner::GetStatusText() const
{
    FString Text=LoadedDesign?FString::Printf(TEXT("素材：%s  ·  修订 %d%s\n"),*LoadedDesign->GetName(),LoadedDesign->Revision,bDraftDirty?TEXT("  ·  草稿尚未保存"):TEXT(""))
        : TEXT("未保存的新建筑草稿\n");
    Text+=LastAction;
    if(PreviewBuilding)
    {
        Text+=FString::Printf(TEXT("\n房间 %d · 合法附件 %d · 无效附件 %d · 自动支柱 %d"),PreviewBuilding->CellCount,
            PreviewBuilding->ValidBlockCount,PreviewBuilding->InvalidBlockCount,PreviewBuilding->SupportColumnCount);
        for(int32 Index=0;Index<FMath::Min(PreviewBuilding->ValidationMessages.Num(),5);++Index)
            Text+=TEXT("\n")+PreviewBuilding->ValidationMessages[Index];
    }
    if(HoveredCell.IsSet()) Text+=FString::Printf(TEXT("\n鼠标格子：(%d, %d)，编辑平面 %d"),HoveredCell->X,HoveredCell->Y,HoveredCell->Z);
    return FText::FromString(Text);
}

FDesertBlockPlacement SDesertBuildingDesigner::MakeProposal(FIntVector Cell) const
{
    FDesertBlockPlacement Proposal;
    const EDesertBlockType Types[]={EDesertBlockType::RoofPavilion,EDesertBlockType::Dome,
        EDesertBlockType::PotCluster,EDesertBlockType::AwningBay,EDesertBlockType::Stairs};
    if(ActiveTool==8) Proposal.Type=EDesertBlockType::RubbleCluster;
    else if(ActiveTool==9) Proposal.Type=EDesertBlockType::RoofCrown;
    else if(ActiveTool>0 && ActiveTool<=UE_ARRAY_COUNT(Types)) Proposal.Type=Types[ActiveTool-1];
    Proposal.Cell=Cell;
    Proposal.Facing=Facing;
    Proposal.StairLayout=StairLayout;
    Proposal.VariantIndex=ActiveTool==4 ? AwningVariantIndex : (ActiveTool==3?PotVariantIndex:-1);
    if (ActiveTool==3) Proposal.PotPlacement=PotPlacement==EDesertPotPlacement::LegacyCentered ? EDesertPotPlacement::Automatic : PotPlacement;
    Proposal.bEnabled=true;
    return Proposal;
}

FLinearColor SDesertBuildingDesigner::GetHoverColor() const
{
    if(ActiveTool==0 && HoveredCell.IsSet() && PreviewBuilding && PreviewBuilding->HasCell(HoveredCell.GetValue())) return FLinearColor(.15f,.8f,1.f);
    if(ActiveTool==3 && HoveredCell.IsSet() && HasPlacedPot(HoveredCell.GetValue())) return FLinearColor(.15f,.8f,1.f);
    return !HoveredCell.IsSet() ? FLinearColor(0.55f,0.75f,0.82f)
        : (HoverCheck.bAllowed ? FLinearColor(0.12f,0.95f,0.38f) : FLinearColor(1.f,0.16f,0.1f));
}

FText SDesertBuildingDesigner::GetHoverText() const
{
    if(!HoveredCell.IsSet()) return LOCTEXT("HoverHint","把鼠标放到编辑格子上查看组合块预览；右键移动相机时暂停放置。");
    if(ActiveTool==0 && PreviewBuilding && PreviewBuilding->HasCell(HoveredCell.GetValue())) return LOCTEXT("RoomSelectHover","已放房间：左键选中，在左栏编辑门窗；Shift+左键或Delete删除。");
    if(ActiveTool==3 && HasPlacedPot(HoveredCell.GetValue())) return LOCTEXT("PotSelectHover","已放瓦罐：左键选中，在左栏调整整组摆放位置；Shift+左键或Delete删除。");
    FString Text=FString::Printf(TEXT("%s  ·  格子 (%d, %d, %d)  ·  %s"),
        HoverCheck.bAllowed ? TEXT("可放置：左键确认") : TEXT("不可放置：点击不会保存隐藏条目"),
        HoveredCell->X,HoveredCell->Y,HoveredCell->Z,*HoverCheck.Reason);
    if(ActiveTool==5 && PreviewBuilding)
        Text+=FString::Printf(TEXT("\n楼梯的格子是目标露台：终点高度 %.0f cm；红色楼梯仍显示预计形状，补齐支撑后需再次点击确认。"),HoveredCell->Z*PreviewBuilding->FloorHeight);
    if((ActiveTool==4 || ActiveTool==3) && HoverCheck.ResolvedVariantIndex>=0 && HoverCheck.CustomMesh)
        Text+=FString::Printf(TEXT("\n%s变体 %d：%s（预览和确认使用同一整组合）"),
            (ActiveTool==3?PotVariantIndex:AwningVariantIndex)==-1?TEXT("自动选择 "):TEXT("明确选择 "),HoverCheck.ResolvedVariantIndex+1,*HoverCheck.CustomMesh->GetName());
    return FText::FromString(Text);
}

FText SDesertBuildingDesigner::GetInvalidDraftText() const
{
    if(!PreviewBuilding) return FText::GetEmpty();
    const int32 Count=PreviewBuilding->InvalidCellIndices.Num()+PreviewBuilding->InvalidBlockIndices.Num()+PreviewBuilding->InvalidRoomAppearanceIndices.Num();
    return Count>0 ? FText::FromString(FString::Printf(TEXT("旧草稿含 %d 个无效输入条目：这些没有生成。请先清理，再添加新块，避免补支撑时突然出现旧楼梯。清理只改草稿，保存后才更新素材。"),Count)) : FText::GetEmpty();
}

void SDesertBuildingDesigner::SetHoveredCell(TOptional<FIntVector> Cell)
{
    const bool bChanged=Cell.IsSet()!=HoveredCell.IsSet() || (Cell.IsSet() && Cell.GetValue()!=HoveredCell.GetValue());
    if(!bChanged && !bHoverNeedsRefresh) return;
    HoveredCell=Cell;
    RefreshHoverPreview();
}

void SDesertBuildingDesigner::ClearGhostGeometry()
{
    for(UInstancedStaticMeshComponent* Component : GhostComponents)
        if(Component) { if(PreviewScene) PreviewScene->RemoveComponent(Component); Component->DestroyComponent(); }
    GhostComponents.Reset();
}

void SDesertBuildingDesigner::AddGhostMesh(UStaticMesh* Mesh,const FTransform& Placement)
{
    if(!Mesh || !PreviewScene || !GhostDynamicMaterial) return;
    UInstancedStaticMeshComponent* Component=nullptr;
    for(UInstancedStaticMeshComponent* Existing : GhostComponents)
        if(Existing && Existing->GetStaticMesh()==Mesh) {Component=Existing;break;}
    if(!Component)
    {
        Component=NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient);
        Component->SetStaticMesh(Mesh);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCollisionResponseToAllChannels(ECR_Ignore);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
        Component->TranslucencySortPriority=10;
        for(int32 Index=0;Index<FMath::Max(1,Mesh->GetStaticMaterials().Num());++Index) Component->SetMaterial(Index,GhostDynamicMaterial);
        GhostComponents.Add(Component);
        PreviewScene->AddComponent(Component,FTransform::Identity);
    }
    Component->AddInstance(Placement,false);
}

void SDesertBuildingDesigner::AddGhostRecipe(EDesertModuleKind Kind,const FTransform& Placement,FVector Dimensions,int32 Steps,bool bWithProps,
    UStaticMesh* ResolvedMesh)
{
    if(UStaticMesh* Custom=ResolvedMesh ? ResolvedMesh : ADesertBuildingModule::GetStyleMesh(DraftStyle,Kind))
    {
        const FVector Reference=ADesertBuildingModule::GetDefaultDimensions(Kind);
        AddGhostMesh(Custom,FTransform(FQuat::Identity,FVector::ZeroVector,Dimensions/Reference)*Placement);
        return;
    }
    TArray<FDesertModulePart> Recipe;
    ADesertBuildingModule::MakeVisualRecipe(Kind,Dimensions,Steps,bWithProps,false,Recipe);
    UStaticMesh* Shapes[]={LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")),
        LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")),
        LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"))};
    for(const FDesertModulePart& Part : Recipe)
        if(Part.Shape>=0 && Part.Shape<UE_ARRAY_COUNT(Shapes)) AddGhostMesh(Shapes[Part.Shape],Part.Transform*Placement);
}

void SDesertBuildingDesigner::RefreshHoverPreview()
{
    bHoverNeedsRefresh=false;
    ClearGhostGeometry();
    HoverCheck=FDesertPlacementCheck();
    RefreshModulePreview();
    if(!HoveredCell.IsSet() || !PreviewBuilding || !GhostRecipeBuilding) return;
    const FDesertBlockPlacement Proposal=MakeProposal(HoveredCell.GetValue());
    if(ActiveTool==0 && PreviewBuilding->HasCell(Proposal.Cell))
    { const FVector Min(Proposal.Cell.X*PreviewBuilding->CellSize,Proposal.Cell.Y*PreviewBuilding->CellSize,Proposal.Cell.Z*PreviewBuilding->FloorHeight);
      HoverCheck.LocalBounds.Add(FBox(Min,Min+FVector(PreviewBuilding->CellSize,PreviewBuilding->CellSize,PreviewBuilding->FloorHeight))); return; }
    if(ActiveTool==3 && HasPlacedPot(Proposal.Cell))
    { const FVector Min(Proposal.Cell.X*PreviewBuilding->CellSize,Proposal.Cell.Y*PreviewBuilding->CellSize,Proposal.Cell.Z*PreviewBuilding->FloorHeight);
      HoverCheck.LocalBounds.Add(FBox(Min,Min+FVector(PreviewBuilding->CellSize,PreviewBuilding->CellSize,PreviewBuilding->FloorHeight))); return; }
    HoverCheck=PreviewBuilding->EvaluatePlacement(ActiveTool==0,Proposal);
    if(ActiveTool==0 && !HoverCheck.Supports.IsEmpty())
    {
        // 承托差集可能含旧高层房间缩短后的柱；线框与模型都只属于当前待放置格。
        const float ExpectedTop=Proposal.Cell.Z==0 ? -PreviewBuilding->FoundationDepth
            : Proposal.Cell.Z*PreviewBuilding->FloorHeight;
        const FVector Center((Proposal.Cell.X+0.5f)*PreviewBuilding->CellSize,
            (Proposal.Cell.Y+0.5f)*PreviewBuilding->CellSize,Proposal.Cell.Z*PreviewBuilding->FloorHeight);
        HoverCheck.LocalBounds.SetNum(FMath::Min(1,HoverCheck.LocalBounds.Num()));
        HoverCheck.Supports.RemoveAll([&](const FDesertPlacementSupport& Span)
        { return !FMath::IsNearlyEqual(Span.Top.Z,ExpectedTop,0.01f) ||
            FMath::Abs(Span.Top.X-Center.X)>PreviewBuilding->CellSize*0.5f ||
            FMath::Abs(Span.Top.Y-Center.Y)>PreviewBuilding->CellSize*0.5f; });
        const FVector Extent(PreviewBuilding->CellSize*0.085f,PreviewBuilding->CellSize*0.085f,0);
        for(const FDesertPlacementSupport& Span:HoverCheck.Supports)
            HoverCheck.LocalBounds.Add(FBox(Span.Bottom-Extent,Span.Top+Extent));
    }
    GhostDynamicMaterial->SetVectorParameterValue(TEXT("PlacementColor"),GetHoverColor());
    if(HoverCheck.bHasVisualRecipe)
    {
        AddGhostRecipe(HoverCheck.ModuleKind,HoverCheck.Placement,HoverCheck.Dimensions,HoverCheck.Steps,
            HoverCheck.ModuleKind==EDesertModuleKind::AwningBay,HoverCheck.CustomMesh);
    }
    else if(ActiveTool==0)
    {
        CopyDraftTo(GhostRecipeBuilding,DraftStyle);
        GhostRecipeBuilding->DesignAsset=nullptr;
        GhostRecipeBuilding->bGenerateOnPlacement=false;
        GhostRecipeBuilding->bEnableAwning=false;
        // 合法房间：同一套生成器保留完整邻接求解，但仅输出当前房格。
        // 非法房间：单房白模/正式模型仍显示在拒绝位置，不会修改真正的草稿。
        if(HoverCheck.bAllowed) GhostRecipeBuilding->Cells.AddUnique(Proposal.Cell);
        else
        {
            GhostRecipeBuilding->Cells={FIntVector(Proposal.Cell.X,Proposal.Cell.Y,0)};
            GhostRecipeBuilding->Blocks.Reset();
            GhostRecipeBuilding->RoofOpenings.Reset();
        }
        GhostRecipeBuilding->RebuildRoomPlacementPreview(HoverCheck.bAllowed
            ? Proposal.Cell : FIntVector(Proposal.Cell.X,Proposal.Cell.Y,0));
        TInlineComponentArray<UInstancedStaticMeshComponent*> Candidate(GhostRecipeBuilding);
        for(UInstancedStaticMeshComponent* Component:Candidate)
        {
            if(!Component || !Component->GetStaticMesh()) continue;
            if(!HoverCheck.bAllowed && GhostRecipeBuilding->GetFloorIndexForComponent(Component)==0) continue;
            for(int32 Index=0;Index<Component->GetInstanceCount();++Index)
            {
                FTransform Transform;
                if(!Component->GetInstanceTransform(Index,Transform,false)) continue;
                if(!HoverCheck.bAllowed) Transform.AddToTranslation(FVector(0,0,Proposal.Cell.Z*PreviewBuilding->FloorHeight));
                AddGhostMesh(Component->GetStaticMesh(),Transform);
            }
        }
        if(HoverCheck.bAllowed)
        {
            // 差集中的缩短旧柱不属于新房格；只显示待放置房间自身的新增承托。
            const float ExpectedTop=Proposal.Cell.Z==0 ? -PreviewBuilding->FoundationDepth
                : Proposal.Cell.Z*PreviewBuilding->FloorHeight;
            for(const FDesertPlacementSupport& Span:HoverCheck.Supports)
            {
                const FVector Center((Proposal.Cell.X+0.5f)*PreviewBuilding->CellSize,
                    (Proposal.Cell.Y+0.5f)*PreviewBuilding->CellSize,Proposal.Cell.Z*PreviewBuilding->FloorHeight);
                if(!FMath::IsNearlyEqual(Span.Top.Z,ExpectedTop,0.01f) ||
                    FMath::Abs(Span.Top.X-Center.X)>PreviewBuilding->CellSize*0.5f ||
                    FMath::Abs(Span.Top.Y-Center.Y)>PreviewBuilding->CellSize*0.5f) continue;
                float BottomZ=Span.Bottom.Z;
                while(BottomZ<Span.Top.Z-KINDA_SMALL_NUMBER)
                {
                    const int32 Floor=BottomZ<0 ? 0 : FMath::FloorToInt((BottomZ+0.01f)/PreviewBuilding->FloorHeight)+1;
                    const float Boundary=Floor==0 ? 0.f : Floor*PreviewBuilding->FloorHeight;
                    const float TopZ=FMath::Min(Span.Top.Z,Boundary);
                    AddGhostRecipe(EDesertModuleKind::Column,FTransform(FVector(Span.Bottom.X,Span.Bottom.Y,BottomZ)),
                        FVector(PreviewBuilding->CellSize*0.17f,PreviewBuilding->CellSize*0.17f,TopZ-BottomZ),15,false);
                    BottomZ=TopZ;
                }
            }
        }
    }
    if(PreviewViewport) PreviewViewport->Invalidate();
}

FReply SDesertBuildingDesigner::ClearInvalidDraft()
{
    if(!PreviewBuilding || (GEditor && GEditor->PlayWorld)) return FReply::Handled();
    FScopedTransaction Transaction(LOCTEXT("ClearInvalidTransaction","清理建筑草稿旧的无效输入条目"));
    PreviewBuilding->Modify();
    const int32 Removed=PreviewBuilding->RemoveInvalidAuthoringEntries();
    bDraftDirty|=Removed>0;
    LastAction=FString::Printf(TEXT("已清理 %d 个旧无效条目。草稿可继续放置；清理可撤销，保存后才改变素材。"),Removed);
    RebuildPreview();
    return FReply::Handled();
}

void SDesertBuildingDesigner::FocusFromViewport(){FocusPreview();}

void SDesertBuildingDesigner::RebuildPreview()
{
    bModulePreviewNeedsRefresh=true;
    if(PreviewBuilding)
    {
        if(PreviewGround)
        {
            const float GridSize=FMath::IsFinite(PreviewBuilding->CellSize)
                ? FMath::Clamp(PreviewBuilding->CellSize,100.f,1000.f) : 300.f;
            // 七格编辑范围外再留余量，保证所有四角支柱都能命中可见预览地面。
            PreviewGround->SetWorldLocation(FVector(GridSize*0.5f,GridSize*0.5f,-55.f));
            PreviewGround->SetWorldScale3D(FVector(GridSize*8.f/100.f,GridSize*8.f/100.f,1.f));
        }
        PreviewBuilding->Style=DraftStyle; PreviewBuilding->Rebuild();
    }
    bHoverNeedsRefresh=true;
    RefreshHoverPreview();
    if(PreviewViewport) PreviewViewport->Invalidate();
}

void SDesertBuildingDesigner::ReplaceDraftStyle(const UDesertBuildingStyle* Source)
{
    DraftStyle=NewObject<UDesertBuildingStyle>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional);
    DesertCopyStyle(Source,DraftStyle);
    if(PreviewBuilding) PreviewBuilding->Style=DraftStyle;
    if(AdvancedStyleDetails) AdvancedStyleDetails->SetObject(DraftStyle);
}

void SDesertBuildingDesigner::HandleCellClick(FIntVector Cell,bool bDelete)
{
    if(!PreviewBuilding || (GEditor && GEditor->PlayWorld)) return;
    if (!bDelete && ActiveTool==3)
        for (const FDesertBlockPlacement& Existing : PreviewBuilding->Blocks)
            if (Existing.bEnabled && Existing.Type==EDesertBlockType::PotCluster && Existing.Cell==Cell)
            {
                SelectedPotCell=Cell; PotPlacement=Existing.PotPlacement; Facing=Existing.Facing; PotVariantIndex=Existing.VariantIndex;
                LastAction=TEXT("已选中瓦罐整组；左栏的摆放位置可修改此组，只有合法位置才会写入草稿。继续点击空格可放置新组。");
                bModulePreviewNeedsRefresh=bHoverNeedsRefresh=true; RefreshHoverPreview(); return;
            }
    SelectedPotCell.Reset();
    const FDesertBlockPlacement Proposal=MakeProposal(Cell);
    if(!bDelete && ActiveTool==0 && PreviewBuilding->HasCell(Cell))
    { SelectedRoomCell=Cell; bModulePreviewNeedsRefresh=true; LastAction=TEXT("已选中房间。左栏直接设置门向/无门与四面窗位；橙色框为当前房间，共享面自动隐藏。"); bHoverNeedsRefresh=true; RefreshHoverPreview(); return; }
    if(!bDelete)
    {
        const FDesertPlacementCheck Check=PreviewBuilding->EvaluatePlacement(ActiveTool==0,Proposal);
        if(!Check.bAllowed)
        {
            LastAction=TEXT("未放置：")+Check.Reason+TEXT("。此次点击没有写入任何隐藏条目。");
            HoveredCell=Cell;
            bHoverNeedsRefresh=true;
            RefreshHoverPreview();
            return;
        }
    }
    FScopedTransaction Transaction(bDelete?LOCTEXT("EraseCell","删除建筑草稿组合块"):LOCTEXT("PaintCell","添加建筑草稿组合块"));
    PreviewBuilding->Modify();
    if(bDelete)
    {
        const int32 Removed=PreviewBuilding->RemovePlacementAndDependents(ActiveTool==0,Proposal);
        if(Removed==0) { LastAction=TEXT("该格没有当前所选类型；切换到要删除的组合块类型后再操作。"); return; }
        LastAction=Removed>1 ? FString::Printf(TEXT("已删除当前组合块，并同时移除 %d 个失去承托或冲突的依赖模块；可整体撤销。"),Removed-1)
            : TEXT("已删除当前组合块；Shift + 左键 / Delete 删除，右键始终移动相机。");
    }
    else
    {
        if(!PreviewBuilding->TryAddPlacement(ActiveTool==0,Proposal))
        {LastAction=TEXT("未放置：规则检查未通过，草稿输入没有增加。");return;}
        LastAction=TEXT("已确认放置。绿色预览变为正常建筑；无效点击不会预先存下模块。");
    }
    if(ActiveTool==0 && !bDelete) SelectedRoomCell=Cell;
    if(SelectedRoomCell.IsSet() && !PreviewBuilding->HasCell(SelectedRoomCell.GetValue())) SelectedRoomCell.Reset();
    bDraftDirty=true;
    RebuildPreview();
}

void SDesertBuildingDesigner::LoadDesign(UDesertBuildingDesign* Design)
{
    if(!Design || !PreviewBuilding) return;
    LoadedDesign=Design;
    SelectedRoomCell.Reset();
    SelectedPotCell.Reset(); PotPlacement=EDesertPotPlacement::Automatic;
    Design->ApplyTo(PreviewBuilding,false);
    ReplaceDraftStyle(Design->Style);
    RebuildPreview();
    bDraftDirty=false;
    LastAction=TEXT("已载入独立预览草稿。保存更新会同步此素材的全部绑定实例；复制变体会建立另一份素材。");
    if(UDesertBuildingEditorLibrary::GetLegacySwitchbackCount(PreviewBuilding)>0) LastAction+=TEXT("旧折返数据保留；左栏可显式转换为现代U形，不自动替换。");
    FocusPreview();
}

ADesertBuilding* SDesertBuildingDesigner::FindSelectedBuilding()
{
    if(!GEditor || GEditor->PlayWorld) return nullptr;
    for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
        if(ADesertBuilding* Building=Cast<ADesertBuilding>(*It)) return Building;
    return nullptr;
}

void SDesertBuildingDesigner::CopyDraftTo(ADesertBuilding* Destination,UDesertBuildingStyle* Style) const
{
    if(!Destination || !PreviewBuilding) return;
    Destination->Cells=PreviewBuilding->Cells;Destination->Blocks=PreviewBuilding->Blocks;
    Destination->RoomAppearanceOverrides=PreviewBuilding->RoomAppearanceOverrides;
    Destination->RoofOpenings=PreviewBuilding->RoofOpenings;
    Destination->CellSize=PreviewBuilding->CellSize;Destination->FloorHeight=PreviewBuilding->FloorHeight;
    Destination->FoundationDepth=PreviewBuilding->FoundationDepth;Destination->Seed=PreviewBuilding->Seed;
    Destination->bShowWoodBeams=PreviewBuilding->bShowWoodBeams;Destination->bEnableAwning=PreviewBuilding->bEnableAwning;
    Destination->bAutoSupportColumns=PreviewBuilding->bAutoSupportColumns;Destination->SupportTraceDistance=PreviewBuilding->SupportTraceDistance;
    Destination->bUseEntranceOverride=PreviewBuilding->bUseEntranceOverride;
    Destination->EntranceOverrideCell=PreviewBuilding->EntranceOverrideCell;Destination->EntranceOverrideSide=PreviewBuilding->EntranceOverrideSide;
    Destination->Style=Style;
}

FReply SDesertBuildingDesigner::NewDraft()
{
    if(!PreviewBuilding) return FReply::Handled();
    LoadedDesign=nullptr;
    PreviewBuilding->DesignAsset=nullptr;
    PreviewBuilding->Cells.Empty();PreviewBuilding->Blocks.Empty();PreviewBuilding->RoofOpenings.Empty();
    PreviewBuilding->RoomAppearanceOverrides.Reset(); SelectedRoomCell.Reset();
    PreviewBuilding->bUseEntranceOverride=false;PreviewBuilding->EntranceOverrideCell=FIntVector::ZeroValue;PreviewBuilding->EntranceOverrideSide=0;
    ReplaceDraftStyle(DesertLoadDefaultBuildingStyle());
    bDraftDirty=false;EditFloor=0;AwningVariantIndex=PotVariantIndex=-1;PotPlacement=EDesertPotPlacement::Automatic;SelectedPotCell.Reset();LastAction=TEXT("新的空白草稿。先选择房间体块，在地面格点击搭建。");
    RebuildPreview();
    return FReply::Handled();
}

FReply SDesertBuildingDesigner::LoadSelected()
{
    ADesertBuilding* Selected=FindSelectedBuilding();
    if(!Selected) { LastAction=TEXT("请先在关卡视口或世界大纲中选中本插件的建筑对象。");return FReply::Handled(); }
    if(Selected->DesignAsset) LoadDesign(Selected->DesignAsset);
    else
    {
        LoadedDesign=nullptr;
        UDesertBuildingStyle* SelectedStyle=Selected->Style;
        PreviewBuilding->Cells=Selected->Cells;PreviewBuilding->Blocks=Selected->Blocks;PreviewBuilding->RoofOpenings=Selected->RoofOpenings;
        PreviewBuilding->RoomAppearanceOverrides=Selected->RoomAppearanceOverrides; SelectedRoomCell.Reset();
        PreviewBuilding->CellSize=Selected->CellSize;PreviewBuilding->FloorHeight=Selected->FloorHeight;
        PreviewBuilding->FoundationDepth=Selected->FoundationDepth;PreviewBuilding->Seed=Selected->Seed;
        PreviewBuilding->bShowWoodBeams=Selected->bShowWoodBeams;PreviewBuilding->bEnableAwning=Selected->bEnableAwning;
        PreviewBuilding->bAutoSupportColumns=Selected->bAutoSupportColumns;PreviewBuilding->SupportTraceDistance=Selected->SupportTraceDistance;
        PreviewBuilding->bUseEntranceOverride=Selected->bUseEntranceOverride;
        PreviewBuilding->EntranceOverrideCell=Selected->EntranceOverrideCell;PreviewBuilding->EntranceOverrideSide=Selected->EntranceOverrideSide;
        PreviewBuilding->DesignAsset=nullptr;
        ReplaceDraftStyle(SelectedStyle);RebuildPreview();bDraftDirty=true;
        LastAction=TEXT("已载入旧版/未绑定建筑。请另存新素材，再应用到选中的建筑，建立可持续编辑的绑定。");
        FocusPreview();
    }
    return FReply::Handled();
}

FReply SDesertBuildingDesigner::LoadAssetDialog()
{
    FOpenAssetDialogConfig Dialog;
    Dialog.DialogTitleOverride=LOCTEXT("OpenDesign","载入可编辑建筑素材");
    Dialog.DefaultPath=TEXT("/Game");
    Dialog.AssetClassNames.Add(UDesertBuildingDesign::StaticClass()->GetClassPathName());
    Dialog.bAllowMultipleSelection=false;
    const TArray<FAssetData> Assets=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalOpenAssetDialog(Dialog);
    if(Assets.Num()) LoadDesign(Cast<UDesertBuildingDesign>(Assets[0].GetAsset()));
    return FReply::Handled();
}

FReply SDesertBuildingDesigner::ApplySelected()
{
    ADesertBuilding* Selected=FindSelectedBuilding();
    if(!Selected) {LastAction=TEXT("请先选中关卡中的建筑对象。");return FReply::Handled();}
    if(!LoadedDesign || bDraftDirty) {LastAction=TEXT("请先保存素材。应用按钮会把场景对象绑定到已保存素材，避免引用临时预览数据。");return FReply::Handled();}
    FScopedTransaction Transaction(LOCTEXT("ApplyDesign","绑定场景建筑到保存的素材"));
    Selected->Modify();LoadedDesign->ApplyTo(Selected,true);Selected->MarkPackageDirty();
    GEditor->RedrawLevelEditingViewports();
    LastAction=TEXT("已应用并绑定素材。请保存这个关卡；以后保存更新该素材时，此对象也会同步。");
    return FReply::Handled();
}

FReply SDesertBuildingDesigner::PlaceInLevel()
{
    if(!LoadedDesign || bDraftDirty || !GEditor || GEditor->PlayWorld) return FReply::Handled();
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if(!World) return FReply::Handled();
    UClass* Class=LoadedDesign->BuildingClass.LoadSynchronous();
    if(!Class || !Class->IsChildOf(ADesertBuilding::StaticClass())) Class=ADesertBuilding::StaticClass();
    FScopedTransaction Transaction(LOCTEXT("PlaceDesign","放置已保存的建筑素材"));
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transactional;
    ADesertBuilding* Building=World->SpawnActor<ADesertBuilding>(Class,FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    if(Building)
    {
        Building->Modify();LoadedDesign->ApplyTo(Building,true);Building->SetActorLabel(LoadedDesign->GetName());Building->MarkPackageDirty();
        GEditor->SelectNone(false,true);GEditor->SelectActor(Building,true,true);GEditor->NoteSelectionChange();GEditor->RedrawLevelEditingViewports();
        LastAction=TEXT("已放入关卡原点并选中。用普通移动工具移动建筑，并保存关卡；也可从内容浏览器拖入 BP_ 建筑素材。");
    }
    return FReply::Handled();
}

int32 SDesertBuildingDesigner::RefreshLinkedInstances(UDesertBuildingDesign* Design)
{
    return UDesertBuildingEditorLibrary::RefreshDesignInstances(Design);
}

FReply SDesertBuildingDesigner::SaveAs(){SaveDesign(false);return FReply::Handled();}
FReply SDesertBuildingDesigner::SaveUpdate(){SaveDesign(true);return FReply::Handled();}
FReply SDesertBuildingDesigner::Undo(){if(GEditor)GEditor->UndoTransaction();return FReply::Handled();}
FReply SDesertBuildingDesigner::Redo(){if(GEditor)GEditor->RedoTransaction();return FReply::Handled();}
FReply SDesertBuildingDesigner::FocusPreview(){if(PreviewViewport)PreviewViewport->FocusBuilding();return FReply::Handled();}

bool SDesertBuildingDesigner::SaveDesign(bool bUpdate)
{
    if(!PreviewBuilding || !DraftStyle || (GEditor && GEditor->PlayWorld)) return false;
    UDesertBuildingDesign* Design=bUpdate?LoadedDesign:nullptr;
    if(bUpdate && !Design) return false;
    FString DesignPackageName,Name,Folder;
    if(Design)
    {
        DesignPackageName=Design->GetOutermost()->GetName();Name=Design->GetName();
        Folder=FPackageName::GetLongPackagePath(DesignPackageName);
    }
    else
    {
        FSaveAssetDialogConfig Dialog;
        Dialog.DialogTitleOverride=LOCTEXT("SaveDesignDialog","选择保存根目录与建筑名称：自动分类收纳建筑和美术资源");
        Dialog.DefaultPath=TEXT("/Game/Buildings");
        if(LoadedDesign)
        {
            Dialog.DefaultPath=FPackageName::GetLongPackagePath(LoadedDesign->GetOutermost()->GetName());
            if(LoadedDesign->AssetLayoutVersion==1)
            {
                FString Stem=LoadedDesign->GetName();
                while(Stem.StartsWith(TEXT("DA_")) || Stem.StartsWith(TEXT("BP_"))) Stem.RightChopInline(3);
                const FString Suffix=TEXT("/Buildings/")+Stem+TEXT("/Data");
                if(Dialog.DefaultPath.EndsWith(Suffix)) Dialog.DefaultPath.LeftChopInline(Suffix.Len());
                else Dialog.DefaultPath=LoadedDesign->AssetRoot;
            }
        }
        Dialog.DefaultAssetName=LoadedDesign?LoadedDesign->GetName()+TEXT("_Variant"):TEXT("Building");
        Dialog.AssetClassNames.Add(UDesertBuildingDesign::StaticClass()->GetClassPathName());
        Dialog.ExistingAssetPolicy=ESaveAssetDialogExistingAssetPolicy::Disallow;
        const FString ObjectPath=FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(Dialog);
        if(ObjectPath.IsEmpty()) return false;
        DesignPackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
        Name=FPackageName::GetLongPackageAssetName(DesignPackageName);Folder=FPackageName::GetLongPackagePath(DesignPackageName);
    }
    Design=UDesertBuildingEditorLibrary::SaveManagedDesignAsset(PreviewBuilding,DesignPackageName+TEXT(".")+Name,Design);
    LastAction=UDesertBuildingEditorLibrary::GetLastDesignSaveMessage();
    if(!Design) return false;
    LoadedDesign=Design;PreviewBuilding->DesignAsset=Design;PreviewBuilding->AppliedDesignRevision=Design->Revision;
    ReplaceDraftStyle(Design->Style);RebuildPreview();
    bDraftDirty=false;
    UClass* BuildingClass=Design->BuildingClass.LoadSynchronous();
    UBlueprint* Blueprint=BuildingClass?Cast<UBlueprint>(BuildingClass->ClassGeneratedBy):nullptr;
    TArray<UObject*> Assets={Design,Design->Style.Get()};
    if(Blueprint) Assets.Add(Blueprint);
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToAssets(Assets);
    return true;
}

#undef LOCTEXT_NAMESPACE
