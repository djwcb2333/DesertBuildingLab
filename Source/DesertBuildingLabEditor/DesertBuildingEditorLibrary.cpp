#include "DesertBuildingEditorLibrary.h"
#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"
#include "DesertBuildingDefaults.h"
#include "DesertBuildingDesigner.h"
#include "DesertBuildingMaterialRootComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "ScopedTransaction.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DesertEditorLibrary"

static FString DesertDesignerOpenMessage;
static FString DesertStairMigrationMessage;
static TWeakPtr<SDesertBuildingDesigner> DesertOpenDesigner;

FDesertModulePreviewData UDesertBuildingEditorLibrary::GetModulePreviewData(ADesertBuilding* Source,int32 Tool,
    EDesertStairLayout Layout,int32 VariantIndex,int32 Facing,FIntVector ContextCell)
{ return DesertMakeModulePreviewData(Source,Tool,Layout,VariantIndex,Facing,ContextCell); }

TArray<EDesertStairLayout> UDesertBuildingEditorLibrary::GetAuthoringStairLayouts()
{ return DesertGetAuthoringStairLayouts(); }

int32 UDesertBuildingEditorLibrary::GetLegacySwitchbackCount(ADesertBuilding* Source)
{
    int32 Count=0;
    if (Source) for (const FDesertBlockPlacement& Block : Source->Blocks)
        if (Block.Type==EDesertBlockType::Stairs && Block.StairLayout==EDesertStairLayout::Switchback) ++Count;
    return Count;
}

int32 UDesertBuildingEditorLibrary::MigrateLegacySwitchbackStairs(ADesertBuilding* Source)
{
    DesertStairMigrationMessage.Reset();
    if (!Source || (GEditor && GEditor->PlayWorld)) {DesertStairMigrationMessage=TEXT("没有可编辑草稿或正在运行游戏"); return 0;}
    if (!Source->Style || !Source->Style->ModuleUShapeStairs)
    { DesertStairMigrationMessage=TEXT("先载入带现代U形模型的Style；旧折返数据未改，未替换成白模"); return 0; }
    TArray<FDesertBlockPlacement> Working=Source->Blocks;
    int32 Converted=0,Blocked=0;
    TArray<FString> Reasons;
    for (int32 Index=0;Index<Working.Num();++Index)
    {
        if (Working[Index].Type!=EDesertBlockType::Stairs || Working[Index].StairLayout!=EDesertStairLayout::Switchback) continue;
        FDesertBlockPlacement Proposal=Working[Index]; Proposal.StairLayout=EDesertStairLayout::UShape;
        if (Proposal.bEnabled)
        {
            TArray<FDesertBlockPlacement> Without=Working; Without.RemoveAt(Index);
            FDesertPlacementCheck Check;
            { TGuardValue<TArray<FDesertBlockPlacement>> Scope(Source->Blocks,Without); Check=Source->EvaluatePlacement(false,Proposal); }
            if (!Check.bAllowed) {++Blocked; Reasons.Add(FString::Printf(TEXT("Blocks[%d]保留：%s"),Index,*Check.Reason)); continue;}
        }
        Working[Index]=Proposal; ++Converted;
    }
    if (Converted>0)
    {
        FScopedTransaction Transaction(LOCTEXT("MigrateOldSwitchback","将旧折返草稿明确转换到现代U形"));
        Source->Modify(); Source->Blocks=MoveTemp(Working); Source->Rebuild();
    }
    DesertStairMigrationMessage=FString::Printf(TEXT("转换%d个旧折返到现代U形；%d个不满足新占位/支撑/净高要求，仍保留原数据。转换可撤销，保存后才更新素材。"),Converted,Blocked);
    for (const FString& Reason : Reasons) DesertStairMigrationMessage+=TEXT("\n")+Reason;
    return Converted;
}

FString UDesertBuildingEditorLibrary::GetLastStairMigrationMessage() {return DesertStairMigrationMessage;}

bool UDesertBuildingEditorLibrary::OpenBuildingDesignerForDesign(UDesertBuildingDesign* Design)
{
    if (!Design || !FSlateApplication::IsInitialized() || (GEditor && GEditor->PlayWorld))
    { DesertDesignerOpenMessage=TEXT("没有有效建筑素材、Slate尚未就绪或正在运行游戏"); return false; }
    const TSharedPtr<SDockTab> Tab=FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(TEXT("DesertBuildingDesigner"))));
    if (!Tab || Tab->GetContent()->GetTypeAsString()!=TEXT("SDesertBuildingDesigner"))
    { DesertDesignerOpenMessage=TEXT("真实建筑设计器标签页未创建；未替换任何草稿"); return false; }
    TSharedRef<SDesertBuildingDesigner> Designer=StaticCastSharedRef<SDesertBuildingDesigner>(Tab->GetContent());
    DesertOpenDesigner=Designer;
    return Designer->LoadDesignSafely(Design,DesertDesignerOpenMessage);
}

FString UDesertBuildingEditorLibrary::GetLastDesignerOpenMessage() {return DesertDesignerOpenMessage;}
bool UDesertBuildingEditorLibrary::SetBuildingDesignerPreviewSelection(int32 Tool,EDesertStairLayout Layout,int32 VariantIndex,int32 Facing)
{
    if (TSharedPtr<SDesertBuildingDesigner> Designer=DesertOpenDesigner.Pin()) return Designer->SelectModulePreview(Tool,Layout,VariantIndex,Facing);
    return false;
}

bool UDesertBuildingEditorLibrary::SetBuildingDesignerPotPlacement(EDesertPotPlacement Position)
{
    if (TSharedPtr<SDesertBuildingDesigner> Designer=DesertOpenDesigner.Pin()) return Designer->SetPotPlacementSelection(Position);
    return false;
}
FDesertModulePreviewData UDesertBuildingEditorLibrary::GetBuildingDesignerPreviewData()
{
    if (TSharedPtr<SDesertBuildingDesigner> Designer=DesertOpenDesigner.Pin()) return Designer->GetDisplayedModulePreviewData();
    FDesertModulePreviewData Data; Data.Description=TEXT("未通过安全入口打开设计器"); return Data;
}

static UWorld* DesertEditorWorld()
{
    return GEditor && !GEditor->PlayWorld ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool UDesertBuildingEditorLibrary::ConnectCustomMaterialInputs(UMaterialExpressionCustom* Custom,
    const TArray<UMaterialExpression*>& Sources)
{
    if (!Custom || Custom->Inputs.Num()!=Sources.Num() || Sources.IsEmpty()) return false;
    // 校验所有输入后才修改，避免失败时留下半套连线或跨材质错误引用。
    for (UMaterialExpression* Source : Sources)
        if (!Source || Source==Custom || Source->GetOuter()!=Custom->GetOuter()) return false;
    Custom->Modify();
    for (int32 Index=0; Index<Sources.Num(); ++Index)
        Custom->Inputs[Index].Input.Connect(0,Sources[Index]);
    Custom->MarkPackageDirty();
    for (int32 Index=0; Index<Sources.Num(); ++Index)
        if (Custom->Inputs[Index].Input.Expression!=Sources[Index] || Custom->Inputs[Index].Input.OutputIndex!=0)
            return false;
    return true; // 材质重编译/保存由调用者明确完成，接口本身不隐藏额外写入。
}

TArray<FString> UDesertBuildingEditorLibrary::GetCustomMaterialInputConnections(UMaterialExpressionCustom* Custom)
{
    TArray<FString> Result;
    if (!Custom) return Result;
    for (const FCustomInput& Input : Custom->Inputs)
        Result.Add(FString::Printf(TEXT("%s|%s|%d"),*Input.InputName.ToString(),
            Input.Input.Expression ? *Input.Input.Expression->GetPathName() : TEXT("<null>"),Input.Input.OutputIndex));
    return Result;
}

TArray<float> UDesertBuildingEditorLibrary::GetMeshVertexColorStatistics(UStaticMesh* Mesh)
{
    TArray<float> Result;
    Result.Init(0.0f, 13);
    // 不隐式完成异步构建或修改CPU访问属性；调用方须在导入/编译完成后检查。
    if (!Mesh || Mesh->IsCompiling()) return Result;
    const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
    if (!RenderData || RenderData->LODResources.IsEmpty()) return Result;
    const FColorVertexBuffer& Colors = RenderData->LODResources[0].VertexBuffers.ColorVertexBuffer;
    const uint32 Count = Colors.GetNumVertices();
    if (Count == 0 || !Colors.GetVertexData() || Colors.GetStride() < sizeof(FColor)) return Result;

    float Minima[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float Maxima[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    double Sums[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (uint32 Index = 0; Index < Count; ++Index)
    {
        const FColor& Color = Colors.VertexColor(Index);
        const uint8 Channels[4] = { Color.R, Color.G, Color.B, Color.A };
        for (int32 Channel = 0; Channel < 4; ++Channel)
        {
            const float Value = static_cast<float>(Channels[Channel]) / 255.0f;
            Minima[Channel] = FMath::Min(Minima[Channel], Value);
            Maxima[Channel] = FMath::Max(Maxima[Channel], Value);
            Sums[Channel] += static_cast<double>(Value);
        }
    }
    Result[0] = static_cast<float>(Count);
    for (int32 Channel = 0; Channel < 4; ++Channel)
    {
        const int32 Start = 1 + Channel * 3;
        Result[Start] = Minima[Channel];
        Result[Start + 1] = Maxima[Channel];
        Result[Start + 2] = static_cast<float>(Sums[Channel] / static_cast<double>(Count));
    }
    return Result;
}

ADesertBuilding* UDesertBuildingEditorLibrary::CreateBuilding(FVector Location)
{
    UWorld* World = DesertEditorWorld();
    if (!World) return nullptr;
    FScopedTransaction Transaction(LOCTEXT("CreateBuilding", "添加模块化建筑"));
    ADesertBuilding* Building = World->SpawnActor<ADesertBuilding>(Location, FRotator::ZeroRotator);
    if (!Building) return nullptr;
    Building->SetFlags(RF_Transactional);
    Building->Modify();
    Building->Style = DesertLoadDefaultBuildingStyle();
    Building->SetActorLabel(TEXT("Desert_Building"));
    Building->Rebuild();
    Building->MarkPackageDirty();
    return Building;
}

ADesertBuildingModule* UDesertBuildingEditorLibrary::AddModule(ADesertBuilding* Parent, EDesertModuleKind Kind, FTransform LocalTransform)
{
    UWorld* World = DesertEditorWorld();
    if (!World || (Parent && Parent->GetWorld() != World)) return nullptr;
    FScopedTransaction Transaction(LOCTEXT("AddModule", "添加建筑附件"));
    const FTransform WorldTransform = Parent ? LocalTransform * Parent->GetActorTransform() : LocalTransform;
    ADesertBuildingModule* Module = World->SpawnActor<ADesertBuildingModule>(ADesertBuildingModule::StaticClass(), WorldTransform);
    if (!Module) return nullptr;
    Module->SetFlags(RF_Transactional);
    Module->Modify();
    Module->ModuleKind = Kind;
    Module->Style = Parent ? Parent->Style.Get() : DesertLoadDefaultBuildingStyle();
    Module->ApplyKindDefaults();
    if (Parent)
    {
        Parent->Modify();
        Module->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
    }
    Module->SetActorLabel(TEXT("Module_") + StaticEnum<EDesertModuleKind>()->GetNameStringByValue(static_cast<int64>(Kind)));
    Module->MarkPackageDirty();
    return Module;
}

ADesertBuildingModule* UDesertBuildingEditorLibrary::AddStairsToSelectedRoof(ADesertBuilding* Parent)
{
    if (!Parent || !DesertEditorWorld()) return nullptr;
    const FIntVector Cell = Parent->SelectedCell;
    const int32 Side = Parent->SelectedRoofSide;
    const FIntVector Directions[4] = {FIntVector(0,-1,0), FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(-1,0,0)};
    if (Side < 0 || Side > 3 || !Parent->HasCell(Cell) || Parent->HasCell(Cell + FIntVector(0,0,1)) || Parent->HasCell(Cell + Directions[Side])) return nullptr;
    // 该便捷命令从此格的楼层底面起步。高层起点是否有露台支撑，由作者检查。
    FScopedTransaction Transaction(LOCTEXT("AddStairs", "添加露台楼梯和矮墙入口"));
    const FVector Outward(Directions[Side].X, Directions[Side].Y, 0);
    const float Rise = Parent->FloorHeight;
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(Rise / 20.0f), 4, 64);
    const float Run = Steps * 30.0f;
    const FVector Edge((Cell.X + 0.5f) * Parent->CellSize, (Cell.Y + 0.5f) * Parent->CellSize, Cell.Z * Rise);
    const FVector Bottom = Edge + Outward * (Parent->CellSize * 0.5f + Run);
    ADesertBuildingModule* Module = AddModule(Parent, EDesertModuleKind::Stairs,
        FTransform(FRotator(0, Side * 90.0f, 0), Bottom));
    if (!Module) return nullptr;
    Module->Dimensions = FVector(120.0f, Run, Rise);
    Module->StepsCount = Steps;
    Module->Rebuild();
    Parent->SetRoofOpening(Cell, Side, 150.0f);
    return Module;
}

AActor* UDesertBuildingEditorLibrary::BakeBuilding(ADesertBuilding* Source, FVector Offset)
{
    UWorld* World = DesertEditorWorld();
    if (!World || !Source || Source->GetWorld() != World) return nullptr;
    TArray<AActor*> Actors;
    Source->GetAttachedActors(Actors, true, true);
    // 只冻结本工具的建筑附件，避免无意复制用户挂入的任意游戏Actor。
    Actors.RemoveAll([](AActor* Actor) { return !Actor->IsA<ADesertBuildingModule>(); });
    Actors.Insert(Source, 0);
    TArray<UInstancedStaticMeshComponent*> Components;
    // 每个Actor分别读取，不能用下一次GetComponents覆盖前一个的组件集合。
    for (AActor* Actor : Actors)
    {
        TArray<UInstancedStaticMeshComponent*> One;
        Actor->GetComponents(One);
        Components.Append(One);
    }
    for (UInstancedStaticMeshComponent* Component : Components)
    {
        if (!Component->GetStaticMesh() || Component->GetInstanceCount() == 0) continue;
        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
        {
            if (Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Index)))
            {
                UE_LOG(LogTemp, Warning, TEXT("Desert bake: assign persistent Style materials, restore visible amount 1, then Rebuild before baking."));
                return nullptr;
            }
        }
    }
    FScopedTransaction Transaction(LOCTEXT("Bake", "复制建筑为静态实例快照"));
    AActor* Result = World->SpawnActor<AActor>(AActor::StaticClass(), Source->GetActorTransform());
    if (!Result) return nullptr;
    Result->SetFlags(RF_Transactional);
    Result->Modify();
    Result->SetActorLabel(Source->GetActorLabel() + TEXT("_Frozen"));
    Result->Tags.Add(TEXT("DesertFrozenSnapshot"));
    UDesertBuildingMaterialRootComponent* Root = NewObject<UDesertBuildingMaterialRootComponent>(
        Result, TEXT("FrozenRoot"), RF_Transactional);
    Result->AddInstanceComponent(Root);
    Result->SetRootComponent(Root);
    Root->SetMobility(EComponentMobility::Static);
    Root->RegisterComponent();
    // AActor无默认根，Spawn时的transform未必可保留，建立根后明确设置。
    FTransform OutputTransform = Source->GetActorTransform();
    OutputTransform.AddToTranslation(Offset);
    Result->SetActorTransform(OutputTransform);
    int32 ComponentIndex = 0;
    for (UInstancedStaticMeshComponent* Input : Components)
    {
        if (!Input->GetStaticMesh() || Input->GetInstanceCount() == 0) continue;
        UInstancedStaticMeshComponent* Output = NewObject<UInstancedStaticMeshComponent>(Result,
            FName(*FString::Printf(TEXT("Frozen_%d_%s"),ComponentIndex++,*Input->GetName())), RF_Transactional);
        Result->AddInstanceComponent(Output);
        Output->SetupAttachment(Root);
        Output->SetMobility(EComponentMobility::Static);
        Output->SetStaticMesh(Input->GetStaticMesh());
        Output->SetCollisionEnabled(Input->GetCollisionEnabled());
        Output->SetCollisionObjectType(Input->GetCollisionObjectType());
        Output->SetCollisionResponseToChannels(Input->GetCollisionResponseToChannels());
        Output->SetGenerateOverlapEvents(Input->GetGenerateOverlapEvents());
        Output->SetCanEverAffectNavigation(Input->CanEverAffectNavigation());
        Output->SetCastShadow(Input->CastShadow);
        Output->ComponentTags = Input->ComponentTags;
        for (int32 MaterialIndex=0; MaterialIndex<Input->GetNumMaterials(); ++MaterialIndex)
            Output->SetMaterial(MaterialIndex, Input->GetMaterial(MaterialIndex));
        // 保留用户其他CPD通道；共同建筑坐标0..15最后由快照根按新位置重写。
        Output->SetDefaultCustomPrimitiveDataFloatArray(0, Input->GetCustomPrimitiveData().Data);
        Output->RegisterComponent();
        for (int32 InstanceIndex=0; InstanceIndex<Input->GetInstanceCount(); ++InstanceIndex)
        {
            FTransform Instance;
            Input->GetInstanceTransform(InstanceIndex, Instance, true);
            Instance.AddToTranslation(Offset);
            Output->AddInstance(Instance, true);
        }
        Output->UpdateBounds();
        Output->MarkRenderStateDirty();
    }
    Root->RefreshMaterialCoordinates();
    Result->MarkPackageDirty();
    return Result;
}
#undef LOCTEXT_NAMESPACE
