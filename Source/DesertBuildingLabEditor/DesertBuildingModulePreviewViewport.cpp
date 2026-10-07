#include "DesertBuildingModulePreviewViewport.h"
#include "DesertBuildingMaterialRootComponent.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "InputKeyEventArgs.h"

class FDesertModulePreviewClient : public FEditorViewportClient
{
public:
    FDesertModulePreviewClient(FAdvancedPreviewScene* Scene,const TSharedRef<SDesertModulePreviewViewport>& Widget)
        : FEditorViewportClient(nullptr,Scene,Widget),Owner(Widget)
    {
        SetViewRotation(FRotator(-22,130,0)); SetViewLocation(FVector(600,-800,600));
        SetRealtime(true); EngineShowFlags.SetGrid(false); EngineShowFlags.SetSelectionOutline(false);
        bUsingOrbitCamera=true;
    }
    virtual bool ShouldOrbitCamera() const override {return true;}
    virtual bool InputKey(const FInputKeyEventArgs& Args) override
    {
        if (Args.Key==EKeys::F && Args.Event==IE_Pressed)
            if (TSharedPtr<SDesertModulePreviewViewport> ModelView=Owner.Pin()) {ModelView->FocusModel();return true;}
        // UE原生小视口环绕/缩放，不把点击传给主建筑绘格器。
        return FEditorViewportClient::InputKey(Args);
    }
private:
    TWeakPtr<SDesertModulePreviewViewport> Owner;
};

void SDesertModulePreviewViewport::Construct(const FArguments& Args)
{
    ModelScene=MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues()
        .SetCreatePhysicsScene(false).SetTransactional(false).SetEditor(true));
    ModelScene->SetFloorVisibility(false,true); ModelScene->SetEnvironmentVisibility(false,true);
    ModelScene->SetLightDirection(FRotator(-45,-45,0)); ModelScene->SetLightBrightness(3.f); ModelScene->SetSkyBrightness(1.f);
    FActorSpawnParameters Spawn; Spawn.ObjectFlags=RF_Transient;
    ModelActor=ModelScene->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    ModelActor->SetActorEnableCollision(false);
    UDesertBuildingMaterialRootComponent* Root=NewObject<UDesertBuildingMaterialRootComponent>(ModelActor,NAME_None,RF_Transient);
    ModelActor->SetRootComponent(Root); ModelActor->AddInstanceComponent(Root); Root->RegisterComponent();
    SEditorViewport::Construct(SEditorViewport::FArguments());
}

SDesertModulePreviewViewport::~SDesertModulePreviewViewport()
{
    for (UInstancedStaticMeshComponent* Part : Parts) if (Part) Part->DestroyComponent();
    Parts.Reset(); if (ModelActor) ModelActor->Destroy(); ModelActor=nullptr;
}

TSharedRef<FEditorViewportClient> SDesertModulePreviewViewport::MakeEditorViewportClient()
{return MakeShared<FDesertModulePreviewClient>(ModelScene.Get(),SharedThis(this));}

void SDesertModulePreviewViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(ModelActor); Collector.AddReferencedObject(CurrentData.SelectedMesh);
    for (TObjectPtr<UInstancedStaticMeshComponent>& Part : Parts) Collector.AddReferencedObject(Part);
    for (FDesertModulePreviewPart& Part : CurrentData.Parts)
    { Collector.AddReferencedObject(Part.Mesh); for (TObjectPtr<UMaterialInterface>& Material : Part.Materials) Collector.AddReferencedObject(Material); }
}

void SDesertModulePreviewViewport::SetPreviewData(const FDesertModulePreviewData& Data)
{
    CurrentData=Data;
    for (UInstancedStaticMeshComponent* Part : Parts) if (Part) Part->DestroyComponent();
    Parts.Reset();
    if (!ModelActor) return;
    for (const FDesertModulePreviewPart& Input : Data.Parts)
    {
        if (!Input.Mesh) continue;
        UInstancedStaticMeshComponent* Part=NewObject<UInstancedStaticMeshComponent>(ModelActor,NAME_None,RF_Transient);
        ModelActor->AddInstanceComponent(Part); Part->SetupAttachment(ModelActor->GetRootComponent());
        Part->SetMobility(EComponentMobility::Movable); Part->SetStaticMesh(Input.Mesh);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetGenerateOverlapEvents(false); Part->SetCanEverAffectNavigation(false);
        for (int32 Slot=0;Slot<Input.Materials.Num();++Slot) Part->SetMaterial(Slot,Input.Materials[Slot]);
        Part->RegisterComponent(); Part->AddInstance(Input.Transform,false); Parts.Add(Part);
    }
    UDesertBuildingMaterialRootComponent::ApplyToActor(ModelActor);
    FocusModel(); GetViewportClient()->Invalidate();
}

void SDesertModulePreviewViewport::FocusModel()
{
    if (CurrentData.Bounds.IsValid)
    {GetViewportClient()->SetViewRotation(FRotator(-22,130,0));GetViewportClient()->FocusViewportOnBox(CurrentData.Bounds.ExpandBy(15),true);}
}
