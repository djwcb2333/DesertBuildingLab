#include "DesertBuildingEditorLibrary.h"
#include "DesertBuildingDesign.h"
#include "DesertBuilding.h"
#include "DesertBuildingStyle.h"
#include "DesertBuildingAssetCollection.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

static FString DesertDesignSaveMessage;

static bool DesertPackageExists(const FString& PackageName)
{
    return FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr,*PackageName)!=nullptr;
}

FString UDesertBuildingEditorLibrary::GetLastDesignSaveMessage()
{
    return DesertDesignSaveMessage;
}

int32 UDesertBuildingEditorLibrary::RefreshDesignInstances(UDesertBuildingDesign* Design)
{
    if(!Design || !GEditor || GEditor->PlayWorld) return 0;
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if(!World) return 0;
    int32 Count=0;
    for(TActorIterator<ADesertBuilding> It(World);It;++It)
        if(It->DesignAsset==Design)
        {
            It->Modify();It->ApplyLinkedDesign(true);It->MarkPackageDirty();++Count;
        }
    GEditor->RedrawLevelEditingViewports();
    return Count;
}

static UDesertBuildingDesign* DesertSaveDesign(ADesertBuilding* Draft,FString ObjectPath,UDesertBuildingDesign* Existing,bool bManagedNew)
{
    DesertDesignSaveMessage.Empty();
    if(!Draft || !Draft->Style || (GEditor && GEditor->PlayWorld))
    {
        DesertDesignSaveMessage=TEXT("需要可编辑建筑草稿及美术 Style；运行游戏时不能保存建筑素材。");return nullptr;
    }
    TSet<FIntVector> UniqueCells;
    for(const FIntVector& Cell:Draft->Cells) UniqueCells.Add(Cell);
    if(Draft->InvalidBlockCount>0 || !Draft->InvalidCellIndices.IsEmpty() || !Draft->InvalidRoomAppearanceIndices.IsEmpty() || Draft->CellCount!=UniqueCells.Num())
    {
        DesertDesignSaveMessage=TEXT("保存前请清理或修复草稿中的无效条目；可点清理旧无效条目，或选择对应类型用Shift+左键/Delete移除。");return nullptr;
    }
    if(Existing) ObjectPath=Existing->GetPathName();
    // Older editor scripts pass a long package name without an object suffix.
    if(!ObjectPath.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(ObjectPath))
        ObjectPath+=TEXT(".")+FPackageName::GetLongPackageAssetName(ObjectPath);
    FText PathReason;
    if(!FPackageName::IsValidObjectPath(ObjectPath,&PathReason))
    {
        DesertDesignSaveMessage=TEXT("资源路径无效：")+PathReason.ToString();return nullptr;
    }
    FString DesignPackageName=FPackageName::ObjectPathToPackageName(ObjectPath);
    FString Name=FPackageName::GetLongPackageAssetName(DesignPackageName);
    const FString Folder=FPackageName::GetLongPackagePath(DesignPackageName);
    if(FPackageName::ObjectPathToObjectName(ObjectPath)!=Name)
    {DesertDesignSaveMessage=TEXT("资源包名与对象名不一致，请从保存对话框重新选择名称。");return nullptr;}
    if(!DesignPackageName.StartsWith(TEXT("/Game/")))
    {
        DesertDesignSaveMessage=TEXT("请选择项目 Content 的文件夹（/Game）。");return nullptr;
    }
    const bool bManaged=Existing?Existing->AssetLayoutVersion==1:bManagedNew;
    if(Existing && Existing->AssetLayoutVersion>1)
    { DesertDesignSaveMessage=TEXT("该素材使用更新的资源目录格式；当前插件不会降级或改写它。");return nullptr; }
    FString AssetRoot=Folder,Stem=Name;
    FString StyleName=TEXT("DA_")+Name+TEXT("_Style"),BlueprintName=TEXT("BP_")+Name;
    FString StylePackageName=Folder/StyleName,BlueprintPackageName=Folder/BlueprintName;
    if(bManaged)
    {
        while(Stem.StartsWith(TEXT("DA_")) || Stem.StartsWith(TEXT("BP_"))) Stem.RightChopInline(3);
        if(Stem.IsEmpty()) {DesertDesignSaveMessage=TEXT("请输入建筑名称，不能只有 DA_ 或 BP_ 前缀。");return nullptr;}
        if(Existing)
        {
            // Derive from actual paths so moving the whole root in the Content Browser remains supported.
            const FString Suffix=TEXT("/Buildings/")+Stem+TEXT("/Data");
            if(!Folder.EndsWith(Suffix))
            {DesertDesignSaveMessage=TEXT("建筑资源组被部分移动或改名；请另存新素材，或在内容浏览器中恢复完整的 Buildings/建筑名/Data 结构。");return nullptr;}
            AssetRoot=Folder.LeftChop(Suffix.Len());
        }
        if(AssetRoot!=TEXT("/Game") && !AssetRoot.StartsWith(TEXT("/Game/")))
        {DesertDesignSaveMessage=TEXT("收纳目标必须是当前项目 Content 内的文件夹。");return nullptr;}
        Name=TEXT("DA_")+Stem;StyleName=Name+TEXT("_Style");BlueprintName=TEXT("BP_")+Stem;
        const FString BuildingFolder=AssetRoot/TEXT("Buildings")/Stem;
        DesignPackageName=BuildingFolder/TEXT("Data")/Name;
        StylePackageName=BuildingFolder/TEXT("Styles")/StyleName;
        BlueprintPackageName=BuildingFolder/TEXT("Blueprints")/BlueprintName;
        for(const FString& PackageName:{DesignPackageName,StylePackageName,BlueprintPackageName})
            if(!FPackageName::IsValidLongPackageName(PackageName))
            {DesertDesignSaveMessage=TEXT("生成的资源路径无效或过长，请缩短保存目录或建筑名称。");return nullptr;}
        if(Existing && Existing->GetOutermost()->GetName()!=DesignPackageName)
        {DesertDesignSaveMessage=TEXT("建筑数据名称不符合当前资源组；请另存新素材进行整理。");return nullptr;}
    }
    if(!Existing && (DesertPackageExists(DesignPackageName) || DesertPackageExists(StylePackageName) || DesertPackageExists(BlueprintPackageName)))
    {
        DesertDesignSaveMessage=TEXT("这个名称的建筑信息、Style 或 BP 已存在，请换一个新名称；不会覆盖已有素材。");return nullptr;
    }
    UDesertBuildingStyle* PersistentStyle=Existing?Existing->Style.Get():nullptr;
    const bool bOwnsStyle=PersistentStyle && !PersistentStyle->HasAnyFlags(RF_Transient)
        && PersistentStyle->GetOutermost()->GetName()==StylePackageName;
    if(!bOwnsStyle && DesertPackageExists(StylePackageName))
    {
        DesertDesignSaveMessage=TEXT("配套 Style 名称被其他资源占用，请另存新素材。");return nullptr;
    }
    UBlueprint* Blueprint=nullptr;
    if(Existing && !Existing->BuildingClass.IsNull())
        if(UClass* Class=Existing->BuildingClass.LoadSynchronous())
            Blueprint=Cast<UBlueprint>(Class->ClassGeneratedBy);
    if(Blueprint && (Blueprint->GetOutermost()->GetName()!=BlueprintPackageName || !Blueprint->ParentClass
        || !Blueprint->ParentClass->IsChildOf(ADesertBuilding::StaticClass())))
    {
        DesertDesignSaveMessage=TEXT("此素材引用的建筑 BP 不属于它的配套资源，请另存新素材，避免修改其他蓝图。");return nullptr;
    }
    if(!Blueprint && DesertPackageExists(BlueprintPackageName))
    {
        DesertDesignSaveMessage=TEXT("配套 BP 名称被其他资源占用，请另存新素材。");return nullptr;
    }
    FDesertCollectedAssets Collected;
    if(bManaged && !DesertCollectStyleAssets(Draft->Style.Get(),AssetRoot,Collected,DesertDesignSaveMessage)) return nullptr;
    if(!bOwnsStyle)
    {
        PersistentStyle=NewObject<UDesertBuildingStyle>(CreatePackage(*StylePackageName),*StyleName,RF_Public|RF_Standalone|RF_Transactional);
        FAssetRegistryModule::AssetCreated(PersistentStyle);
    }
    PersistentStyle->Modify();
    for(TFieldIterator<FProperty> It(UDesertBuildingStyle::StaticClass());It;++It)
        if(It->GetOwnerClass()==UDesertBuildingStyle::StaticClass()) It->CopyCompleteValue_InContainer(PersistentStyle,Draft->Style.Get());
    if(bManaged) DesertRemapCollectedReferences(PersistentStyle,Collected.Replacements);
    PersistentStyle->MarkPackageDirty();
    UDesertBuildingDesign* Design=Existing;
    if(!Design)
    {
        Design=NewObject<UDesertBuildingDesign>(CreatePackage(*DesignPackageName),*Name,RF_Public|RF_Standalone|RF_Transactional);
        FAssetRegistryModule::AssetCreated(Design);
    }
    Design->Modify();Design->CaptureFrom(Draft);Design->Style=PersistentStyle;
    if(bManaged)
    {
        Design->AssetRoot=AssetRoot;Design->AssetLayoutVersion=1;Design->CollectedAssets=Collected.Assets;
    }
    if(Existing) ++Design->Revision;
    if(!Blueprint)
    {
        Blueprint=FKismetEditorUtilities::CreateBlueprint(ADesertBuilding::StaticClass(),CreatePackage(*BlueprintPackageName),*BlueprintName,BPTYPE_Normal,NAME_None);
        if(!Blueprint) {DesertDesignSaveMessage=TEXT("建立可拖入的建筑蓝图失败，保存未完成。");return nullptr;}
        FAssetRegistryModule::AssetCreated(Blueprint);
    }
    Blueprint->Modify();FKismetEditorUtilities::CompileBlueprint(Blueprint);
    ADesertBuilding* Defaults=Blueprint->GeneratedClass?Cast<ADesertBuilding>(Blueprint->GeneratedClass->GetDefaultObject()):nullptr;
    if(!Defaults) {DesertDesignSaveMessage=TEXT("建筑蓝图未编译出默认对象，保存未完成。");return nullptr;}
    Defaults->Modify();Design->ApplyTo(Defaults,false);Defaults->bGenerateOnPlacement=true;
    // CDO只有制作输入，没有已生成几何，不能标记为已应用任何素材修订。
    // 场景实例Rebuild后的正数修订必须与CDO不同，保证旧地图保存的修订不会被差量序列化省略。
    Defaults->AppliedDesignRevision=0;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Defaults=Cast<ADesertBuilding>(Blueprint->GeneratedClass->GetDefaultObject());
    Design->ApplyTo(Defaults,false);Defaults->bGenerateOnPlacement=true;
    Defaults->AppliedDesignRevision=0;
    Design->BuildingClass=Blueprint->GeneratedClass;
    Design->MarkPackageDirty();Blueprint->MarkPackageDirty();
    TArray<UPackage*> Packages=Collected.NewPackages;
    Packages.AddUnique(PersistentStyle->GetOutermost());Packages.AddUnique(Design->GetOutermost());Packages.AddUnique(Blueprint->GetOutermost());
    if(!UEditorLoadingAndSavingUtils::SavePackages(Packages,false))
    {
        DesertDesignSaveMessage=TEXT("磁盘保存未完成。资源仍在编辑器中，请检查输出日志和目录权限；尚未传播到关卡实例。");return nullptr;
    }
    const int32 Count=UDesertBuildingEditorLibrary::RefreshDesignInstances(Design);
    DesertDesignSaveMessage=bManaged
        ? FString::Printf(TEXT("已分类保存到 %s：Buildings/%s 内为建筑数据、Style 和 BP；Resources 内新收纳 %d 件、复用 %d 件美术资源。当前关卡同步 %d 个绑定对象，请保存关卡。单独编辑过的材质仍需在材质窗口保存。"),*AssetRoot,*Stem,Collected.Created,Collected.Reused,Count)
        : FString::Printf(TEXT("已按旧资源位置保存建筑信息、Style 和 BP；当前关卡同步 %d 个绑定对象。若要收纳为新的分类目录，请另存新素材。请保存关卡。"),Count);
    return Design;
}

UDesertBuildingDesign* UDesertBuildingEditorLibrary::SaveDesignAsset(ADesertBuilding* Draft,FString ObjectPath,UDesertBuildingDesign* Existing)
{
    return DesertSaveDesign(Draft,MoveTemp(ObjectPath),Existing,true);
}

UDesertBuildingDesign* UDesertBuildingEditorLibrary::SaveManagedDesignAsset(ADesertBuilding* Draft,FString ObjectPath,UDesertBuildingDesign* Existing)
{
    return DesertSaveDesign(Draft,MoveTemp(ObjectPath),Existing,true);
}
