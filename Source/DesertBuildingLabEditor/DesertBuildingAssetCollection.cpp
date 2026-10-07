#include "DesertBuildingAssetCollection.h"

#include "DesertBuildingStyle.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/SubsurfaceProfile.h"
#include "Engine/SpecularProfile.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Containers/StringConv.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DesertAssetCollection
{
    static const TCHAR* SourcePackageKey = TEXT("Desert.SourcePackage");
    static const TCHAR* SourceNameKey = TEXT("Desert.SourceAssetName");
    static const TCHAR* CollectionRootKey = TEXT("Desert.CollectionRoot");

    bool IsBuiltIn(const FString& Package)
    {
        return Package.StartsWith(TEXT("/Engine/")) || Package.StartsWith(TEXT("/Script/"));
    }

    bool IsUnderRoot(const FString& Package, const FString& Root)
    {
        return Package.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
    }

    /** A deliberate allow-list: world/Blueprint/Design assets are never duplicated through art slots. */
    FString Category(UObject* Asset)
    {
        if (Asset->IsA<UStaticMesh>()) return TEXT("Meshes");
        if (Asset->IsA<UMaterialInterface>() && !Asset->IsA<UMaterialInstanceDynamic>()) return TEXT("Materials");
        if (Asset->IsA<UMaterialFunctionInterface>()) return TEXT("MaterialFunctions");
        if (Asset->IsA<UTexture>()) return TEXT("Textures");
        if (Asset->IsA<UPhysicalMaterial>() || Asset->IsA<UMaterialParameterCollection>() ||
            Asset->IsA<USubsurfaceProfile>() || Asset->IsA<USpecularProfile>()) return TEXT("Other");
        return FString();
    }

    void GetOwnedObjects(UObject* Asset, TArray<UObject*>& Objects)
    {
        Objects.Add(Asset);
        GetObjectsWithOuter(Asset, Objects, EGetObjectsFlags::IncludeNestedObjects, RF_Transient);
    }

    /** Serialize the live data as well as reflected references; this sees unsaved material edits and soft paths. */
    class FReadReferences final : public FArchiveUObject
    {
    public:
        TArray<UObject*>& Hard;
        TArray<FSoftObjectPath>& Soft;
        FReadReferences(TArray<UObject*>& InHard, TArray<FSoftObjectPath>& InSoft) : Hard(InHard), Soft(InSoft)
        {
            ArIsObjectReferenceCollector = true;
            ArIgnoreOuterRef = true;
            ArIgnoreArchetypeRef = true;
        }
        using FArchiveUObject::operator<<;
        virtual FArchive& operator<<(UObject*& Value) override
        {
            if (Value) Hard.AddUnique(Value);
            return *this;
        }
        virtual FArchive& operator<<(FSoftObjectPath& Value) override
        {
            if (!Value.IsNull()) Soft.AddUnique(Value);
            return *this;
        }
        virtual bool ShouldSkipProperty(const FProperty* Property) const override
        {
            return (Property && Property->HasAnyPropertyFlags(CPF_Transient)) || FArchiveUObject::ShouldSkipProperty(Property);
        }
    };

    /** The base replacement archive resolves soft references only if loaded; explicit paths cover both cases. */
    class FReplaceReferences final : public FArchiveReplaceObjectRef<UObject>
    {
        TMap<FSoftObjectPath, FSoftObjectPath> Paths;
    public:
        FReplaceReferences(UObject* Target, const TMap<UObject*, UObject*>& Map)
            : FArchiveReplaceObjectRef<UObject>(Target, Map,
                EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef |
                EArchiveReplaceObjectFlags::DelayStart)
        {
            for (const auto& Pair : Map)
                if (Pair.Key && Pair.Value) Paths.Add(FSoftObjectPath(Pair.Key), FSoftObjectPath(Pair.Value));
            SerializeSearchObject();
        }
        using FArchiveReplaceObjectRef<UObject>::operator<<;
        virtual FArchive& operator<<(FSoftObjectPath& Value) override
        {
            if (const FSoftObjectPath* Replacement = Paths.Find(Value))
            {
                Value = *Replacement;
                return *this;
            }
            const FSoftObjectPath Owner(Value.GetAssetPath());
            if (const FSoftObjectPath* Replacement = Paths.Find(Owner))
            {
                Value = FSoftObjectPath(Replacement->GetAssetPath(), Value.GetSubPathUtf8String());
                return *this;
            }
            return FArchiveReplaceObjectRef<UObject>::operator<<(Value);
        }
    };

    // Replacing an object used as a map/set key changes its hash. Rehash every owned reflected container.
    void RehashValue(FProperty* Property, void* Value)
    {
        if (FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper Helper(Map, Value);
            for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
                if (Helper.IsValidIndex(I))
                {
                    RehashValue(Map->KeyProp, Helper.GetKeyPtr(I));
                    RehashValue(Map->ValueProp, Helper.GetValuePtr(I));
                }
            Helper.Rehash();
        }
        else if (FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            FScriptSetHelper Helper(Set, Value);
            for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
                if (Helper.IsValidIndex(I)) RehashValue(Set->ElementProp, Helper.GetElementPtr(I));
            Helper.Rehash();
        }
        else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(Array, Value);
            for (int32 I = 0; I < Helper.Num(); ++I) RehashValue(Array->Inner, Helper.GetRawPtr(I));
        }
        else if (FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
                for (int32 I = 0; I < It->ArrayDim; ++I)
                    RehashValue(*It, It->ContainerPtrToValuePtr<void>(Value, I));
        }
    }

    struct FPlannedAsset
    {
        UObject* Source = nullptr;
        UObject* Existing = nullptr;
        FString CanonicalPackage;
        FString CanonicalName;
        FString Package;
        FString Name;
    };

    class FCollector
    {
    public:
        FString Root;
        FString& Error;
        IAssetRegistry& Registry;
        TArray<FPlannedAsset> Plan;
        TMap<UObject*, int32> Visited;
        TMap<FName, UObject*> Destinations;
        // Loading/duplicating may invoke GC indirectly. Keep the live graph alive for this synchronous operation.
        TArray<TStrongObjectPtr<UObject>> KeepAlive;

        FCollector(const FString& InRoot, FString& InError)
            : Root(InRoot), Error(InError), Registry(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get()) {}

        bool Fail(const FString& Message) { Error = Message; return false; }

        bool AddDependency(UObject* Owner, UObject* Reference, TArray<UObject*>& Dependencies)
        {
            if (!Reference || Reference == Owner || Reference->IsIn(Owner) || Reference->IsA<UPackage>()) return true;
            // /Engine/Transient is not a built-in dependency; MIDs must become persistent instances first.
            if (Reference->IsA<UMaterialInstanceDynamic>() || Reference->GetOutermost() == GetTransientPackage())
                return Fail(FString::Printf(TEXT("资源 %s 引用了不能保存的临时对象 %s。请先保存为正式资产后再收纳。"), *Owner->GetPathName(), *Reference->GetPathName()));
            if (IsBuiltIn(Reference->GetOutermost()->GetName())) return true;
            UObject* Asset = Reference;
            while (Asset->GetOuter() && !Asset->GetOuter()->IsA<UPackage>()) Asset = Asset->GetOuter();
            if (Asset == Owner) return true;
            if (Asset->HasAnyFlags(RF_Transient) || Asset->GetOutermost() == GetTransientPackage() ||
                !Asset->IsAsset() || !FPackageName::IsValidLongPackageName(Asset->GetOutermost()->GetName()))
                return Fail(FString::Printf(TEXT("资源 %s 引用了不能保存的临时对象 %s。请先保存为正式资产后再收纳。"), *Owner->GetPathName(), *Reference->GetPathName()));
            if (Category(Asset).IsEmpty())
                return Fail(FString::Printf(TEXT("资源 %s 引用了暂不支持收纳的 %s（类型 %s）。关卡、蓝图、建筑素材/Style 循环及动态材质不能作为美术依赖收纳。"),
                    *Owner->GetPathName(), *Asset->GetPathName(), *Asset->GetClass()->GetName()));
            Dependencies.AddUnique(Asset);
            return true;
        }

        bool ReadDependencies(UObject* Owner, TArray<UObject*>& Dependencies, bool bRegistrySupplement = true)
        {
            TArray<UObject*> Objects;
            GetOwnedObjects(Owner, Objects);
            TArray<UObject*> References;
            TArray<FSoftObjectPath> Soft;
            for (UObject* Object : Objects)
            {
                FReferenceFinder Finder(References, nullptr, false, true, false, true);
                Finder.FindReferences(Object);
                FReadReferences Archive(References, Soft);
                Object->Serialize(Archive);
            }
            for (UObject* Reference : References)
                if (!AddDependency(Owner, Reference, Dependencies)) return false;
            for (const FSoftObjectPath& Path : Soft)
            {
                if (IsBuiltIn(Path.GetLongPackageName())) continue;
                UObject* Reference = Path.TryLoad();
                if (!Reference) return Fail(FString::Printf(TEXT("资源 %s 的软引用无法加载：%s。请修复丢失引用后重试。"), *Owner->GetPathName(), *Path.ToString()));
                if (!AddDependency(Owner, Reference, Dependencies)) return false;
            }
            // Registry data is only a supplement for CLEAN persisted packages. Dirty live objects are authoritative.
            UPackage* Package = Owner->GetOutermost();
            if (bRegistrySupplement && !Package->IsDirty() && Package != GetTransientPackage() &&
                FPackageName::DoesPackageExist(Package->GetName()))
            {
                TArray<FName> Packages;
                Registry.GetDependencies(Package->GetFName(), Packages, UE::AssetRegistry::EDependencyCategory::Package);
                for (FName DependencyPackage : Packages)
                {
                    if (DependencyPackage == Package->GetFName() || IsBuiltIn(DependencyPackage.ToString())) continue;
                    TArray<FAssetData> Assets;
                    Registry.GetAssetsByPackageName(DependencyPackage, Assets);
                    if (Assets.IsEmpty())
                        return Fail(FString::Printf(TEXT("资源 %s 的登记依赖无法解析：%s。请修复重定向或丢失资源后重试。"), *Owner->GetPathName(), *DependencyPackage.ToString()));
                    for (const FAssetData& AssetData : Assets)
                    {
                        UObject* Dependency = AssetData.GetAsset();
                        if (!Dependency) return Fail(FString::Printf(TEXT("无法加载依赖：%s"), *AssetData.GetSoftObjectPath().ToString()));
                        if (!AddDependency(Owner, Dependency, Dependencies)) return false;
                    }
                }
            }
            Dependencies.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
            return true;
        }

        bool PlanAsset(UObject* Asset)
        {
            if (Visited.Contains(Asset)) return true;
            KeepAlive.Emplace(Asset);
            FPlannedAsset Entry;
            Entry.Source = Asset;
            const FString SourcePackage = Asset->GetOutermost()->GetName();
            FMetaData& Metadata = Asset->GetOutermost()->GetMetaData();
            Entry.CanonicalPackage = Metadata.GetValue(Asset, SourcePackageKey);
            Entry.CanonicalName = Metadata.GetValue(Asset, SourceNameKey);
            if (Entry.CanonicalPackage.IsEmpty())
            {
                Entry.CanonicalPackage = SourcePackage;
                Entry.CanonicalName = Asset->GetName();
            }
            if (!FPackageName::IsValidLongPackageName(Entry.CanonicalPackage))
                return Fail(FString::Printf(TEXT("资源来源标记不合法：%s -> %s"), *Asset->GetPathName(), *Entry.CanonicalPackage));
            if (Entry.CanonicalName.IsEmpty()) Entry.CanonicalName = FPackageName::GetLongPackageAssetName(Entry.CanonicalPackage);
            if (IsUnderRoot(SourcePackage, Root))
            {
                Entry.Existing = Asset;
                Entry.Package = SourcePackage;
                Entry.Name = Asset->GetName();
            }
            else
            {
                // UTF-8 bytes (not ANSI/codepage conversion) keep Chinese package names distinct on every machine.
                const FTCHARToUTF8 CanonicalUtf8(*Entry.CanonicalPackage);
                const FString SourceHash = FMD5::HashBytes(reinterpret_cast<const uint8*>(CanonicalUtf8.Get()), CanonicalUtf8.Length()).Left(12);
                Entry.Name = Entry.CanonicalName + TEXT("_") + SourceHash;
                Entry.Package = Root / TEXT("Resources") / Category(Asset) / Entry.Name;
                FText InvalidReason;
                if (!FPackageName::IsValidLongPackageName(Entry.Package, false, &InvalidReason))
                    return Fail(FString::Printf(TEXT("不能建立收纳路径 %s：%s"), *Entry.Package, *InvalidReason.ToString()));
                const FString ObjectPath = Entry.Package + TEXT(".") + Entry.Name;
                UPackage* LoadedPackage = FindPackage(nullptr, *Entry.Package);
                const bool bOnDisk = FPackageName::DoesPackageExist(Entry.Package);
                TArray<UObject*> PackageObjects;
                if (LoadedPackage) GetObjectsWithOuter(LoadedPackage, PackageObjects, EGetObjectsFlags::None, RF_Transient);
                // Rollback may leave an empty in-memory package; it contains no asset to overwrite.
                if (!PackageObjects.IsEmpty() || bOnDisk)
                {
                    Entry.Existing = LoadObject<UObject>(nullptr, *ObjectPath);
                    if (!Entry.Existing || Entry.Existing->GetClass() != Asset->GetClass() ||
                        !Entry.Existing->GetPathName().Equals(ObjectPath, ESearchCase::IgnoreCase))
                        return Fail(FString::Printf(TEXT("目标路径已被其他资产或包占用，不会覆盖：%s"), *ObjectPath));
                    FMetaData& ExistingMeta = Entry.Existing->GetOutermost()->GetMetaData();
                    const FString PreviousRoot = ExistingMeta.GetValue(Entry.Existing, CollectionRootKey);
                    const bool bValidPreviousRoot = (PreviousRoot == TEXT("/Game") || PreviousRoot.StartsWith(TEXT("/Game/"))) &&
                        FPackageName::IsValidLongPackageName(PreviousRoot);
                    // A complete building folder may have moved. The planned path/name/type plus canonical
                    // provenance establish reuse; old root metadata remains untouched on a reused asset.
                    if (ExistingMeta.GetValue(Entry.Existing, SourcePackageKey) != Entry.CanonicalPackage ||
                        ExistingMeta.GetValue(Entry.Existing, SourceNameKey) != Entry.CanonicalName || !bValidPreviousRoot)
                        return Fail(FString::Printf(TEXT("目标已有同名资源，但不是此建筑收纳的同源资源，不会覆盖：%s。请选用新的建筑文件夹。"), *ObjectPath));
                    KeepAlive.Emplace(Entry.Existing);
                }
            }
            if (UObject** Previous = Destinations.Find(FName(*Entry.Package)))
            {
                if (*Previous != Asset && !Entry.Existing)
                    return Fail(FString::Printf(TEXT("两个不同的源副本指向同一收纳路径：%s 和 %s -> %s。请在 Style 中统一使用其中一个副本。"),
                        *(*Previous)->GetPathName(), *Asset->GetPathName(), *Entry.Package));
            }
            else Destinations.Add(FName(*Entry.Package), Asset);
            Visited.Add(Asset, Plan.Num());
            Plan.Add(Entry); // Insert before following references so legal material/function cycles terminate.

            UObject* Effective = Entry.Existing ? Entry.Existing : Asset;
            TArray<UObject*> Dependencies;
            if (!ReadDependencies(Effective, Dependencies)) return false;
            for (UObject* Dependency : Dependencies)
            {
                if (Entry.Existing && !IsUnderRoot(Dependency->GetOutermost()->GetName(), Root))
                    return Fail(FString::Printf(TEXT("目录内资源 %s 仍引用目录外资源 %s。为保留本地编辑，不会自动改写它；请先将此依赖换成目录内资源，或另存到新的建筑文件夹。"),
                        *Effective->GetPathName(), *Dependency->GetPathName()));
                if (!PlanAsset(Dependency)) return false;
            }
            return true;
        }
    };

    /** Only newly-created, never-saved objects enter here. No filesystem deletion or source mutation. */
    void DiscardNewCopies(TArray<UObject*>& Copies, FString& Error)
    {
        TArray<FString> Leftovers;
        for (int32 I = Copies.Num() - 1; I >= 0; --I)
        {
            UObject* Copy = Copies[I];
            if (!IsValid(Copy)) continue;
            UPackage* Package = Copy->GetOutermost();
            const FString OldPath = Copy->GetPathName();
            FAssetRegistryModule::AssetDeleted(Copy);
            const FName DiscardName = MakeUniqueObjectName(GetTransientPackage(), Copy->GetClass(), TEXT("DesertDiscardedCopy"));
            if (Copy->Rename(*DiscardName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
            {
                Copy->ClearFlags(RF_Public | RF_Standalone);
                Copy->SetFlags(RF_Transient);
                Copy->MarkAsGarbage();
                Package->SetDirtyFlag(false);
            }
            else
            {
                FAssetRegistryModule::AssetCreated(Copy);
                Leftovers.Add(OldPath);
            }
        }
        Error += Leftovers.IsEmpty() ? TEXT("\n本次已创建的未保存副本已撤销，源资源未修改。")
            : TEXT("\n以下未保存副本未能自动撤销，请手动检查后删除（源资源未修改）：\n") + FString::Join(Leftovers, TEXT("\n"));
    }
}

void DesertRemapCollectedReferences(UObject* Target, const TMap<UObject*, UObject*>& Replacements)
{
    if (!Target || Replacements.IsEmpty()) return;
    // Save the logical entries BEFORE the archive changes pointer keys in-place, then rebuild its hash table.
    UDesertBuildingStyle* Style = Cast<UDesertBuildingStyle>(Target);
    TArray<TPair<UMaterialInterface*, UMaterialInterface*>> Overrides;
    if (Style)
        for (const auto& Pair : Style->ModuleMaterialOverrides) Overrides.Emplace(Pair.Key.Get(), Pair.Value.Get());

    TMap<UObject*, UObject*> ExpandedMap = Replacements;
    for (const auto& Pair : Replacements)
    {
        if (!Pair.Key || !Pair.Value || Pair.Key == Pair.Value) continue;
        TArray<UObject*> Subobjects;
        GetObjectsWithOuter(Pair.Key, Subobjects, EGetObjectsFlags::IncludeNestedObjects, RF_Transient);
        for (UObject* Subobject : Subobjects)
        {
            const FString RelativePath = Subobject->GetPathName(Pair.Key);
            if (UObject* NewSubobject = StaticFindObject(nullptr, Pair.Value, *RelativePath))
                ExpandedMap.Add(Subobject, NewSubobject);
        }
    }
    DesertAssetCollection::FReplaceReferences Archive(Target, ExpandedMap);
    TArray<UObject*> Objects;
    DesertAssetCollection::GetOwnedObjects(Target, Objects);
    for (UObject* Object : Objects)
        for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
            for (int32 I = 0; I < It->ArrayDim; ++I)
                DesertAssetCollection::RehashValue(*It, It->ContainerPtrToValuePtr<void>(Object, I));
    if (Style)
    {
        Style->ModuleMaterialOverrides.Reset();
        for (const auto& Pair : Overrides)
        {
            UObject* const* NewKey = Replacements.Find(Pair.Key);
            UObject* const* NewValue = Replacements.Find(Pair.Value);
            Style->ModuleMaterialOverrides.Add(NewKey ? Cast<UMaterialInterface>(*NewKey) : Pair.Key,
                NewValue ? Cast<UMaterialInterface>(*NewValue) : Pair.Value);
        }
    }
}

bool DesertCollectStyleAssets(UDesertBuildingStyle* Source, const FString& InRoot,
    FDesertCollectedAssets& Out, FString& Error)
{
    using namespace DesertAssetCollection;
    Out = FDesertCollectedAssets();
    Error.Reset();
    FString Root = InRoot;
    Root.TrimStartAndEndInline();
    while (Root.EndsWith(TEXT("/"))) Root.LeftChopInline(1);
    if (!IsValid(Source) || !IsInGameThread())
    {
        Error = TEXT("美术收纳必须在编辑器主线程中使用有效的 Style。");
        return false;
    }
    if (!(Root == TEXT("/Game") || Root.StartsWith(TEXT("/Game/"))) || !FPackageName::IsValidLongPackageName(Root))
    {
        Error = TEXT("请选择 /Game 或其下的建筑文件夹，例如 /Game/Buildings/House_A。");
        return false;
    }
    FCollector Collector(Root, Error);
    Collector.KeepAlive.Emplace(Source);
    TArray<UObject*> Dependencies;
    if (!Collector.ReadDependencies(Source, Dependencies, false)) return false;
    for (UObject* Dependency : Dependencies)
        if (!Collector.PlanAsset(Dependency)) return false;

    // Nothing has been created before this point. Every dependency and destination is now validated.
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TArray<UObject*> NewCopies;
    TSet<UObject*> UniqueReused;
    for (const FPlannedAsset& Entry : Collector.Plan)
    {
        UObject* Target = Entry.Existing;
        if (!Target)
        {
            Target = AssetTools.DuplicateAsset(Entry.Name, FPackageName::GetLongPackagePath(Entry.Package), Entry.Source);
            if (!Target || Target->GetClass() != Entry.Source->GetClass() || Target->GetOutermost()->GetName() != Entry.Package)
            {
                if (Target) NewCopies.Add(Target);
                Error = FString::Printf(TEXT("复制资源失败：%s -> %s"), *Entry.Source->GetPathName(), *Entry.Package);
                DiscardNewCopies(NewCopies, Error);
                Out = FDesertCollectedAssets();
                return false;
            }
            Collector.KeepAlive.Emplace(Target);
            NewCopies.Add(Target);
            FMetaData& Metadata = Target->GetOutermost()->GetMetaData();
            Metadata.SetValue(Target, SourcePackageKey, *Entry.CanonicalPackage);
            Metadata.SetValue(Target, SourceNameKey, *Entry.CanonicalName);
            Metadata.SetValue(Target, CollectionRootKey, *Root);
            Out.NewPackages.AddUnique(Target->GetOutermost());
        }
        else UniqueReused.Add(Target);
        Out.Replacements.Add(Entry.Source, Target);
        Out.Assets.AddUnique(FSoftObjectPath(Target));
    }
    for (UObject* Copy : NewCopies) DesertRemapCollectedReferences(Copy, Out.Replacements);
    // Rebuild derived state only after all new assets have their final, collected references.
    for (UObject* Copy : NewCopies)
    {
        Copy->PostEditChange();
        Copy->MarkPackageDirty();
    }
    // Verify the actual clones too: unsupported custom serialization must never silently leave source refs.
    for (UObject* Copy : NewCopies)
    {
        TArray<UObject*> FinalDependencies;
        if (!Collector.ReadDependencies(Copy, FinalDependencies, false))
        {
            DiscardNewCopies(NewCopies, Error);
            Out = FDesertCollectedAssets();
            return false;
        }
        for (UObject* Dependency : FinalDependencies)
            if (!IsUnderRoot(Dependency->GetOutermost()->GetName(), Root))
            {
                Error = FString::Printf(TEXT("副本仍存在未重定向的外部依赖：%s -> %s。收纳已中止。"), *Copy->GetPathName(), *Dependency->GetPathName());
                DiscardNewCopies(NewCopies, Error);
                Out = FDesertCollectedAssets();
                return false;
            }
    }
    Out.Created = NewCopies.Num();
    Out.Reused = UniqueReused.Num();
    Out.Assets.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
    return true;
}
