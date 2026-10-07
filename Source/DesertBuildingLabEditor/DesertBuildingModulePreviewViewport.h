#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "DesertBuildingModulePreviewData.h"

class FAdvancedPreviewScene;
class AActor;
class UInstancedStaticMeshComponent;

/** 独立展示世界，不参与主预览放置射线、撤销和素材保存。 */
class SDesertModulePreviewViewport : public SEditorViewport, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SDesertModulePreviewViewport) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SDesertModulePreviewViewport() override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override {return TEXT("SDesertModulePreviewViewport");}
    void SetPreviewData(const FDesertModulePreviewData& Data);
    const FDesertModulePreviewData& GetPreviewData() const {return CurrentData;}
    void FocusModel();
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
private:
    TUniquePtr<FAdvancedPreviewScene> ModelScene;
    TObjectPtr<AActor> ModelActor;
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
    FDesertModulePreviewData CurrentData;
};
