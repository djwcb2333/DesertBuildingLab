#include "DesertBuildingModule.h"

#include "DesertBuildingStyle.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace DesertModuleRules
{
    static double SafeDimension(double Value, double Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, 1.0, 5000.0) : Fallback;
    }
    static FVector SanitizeDimensions(const FVector& Value, const FVector& Fallback)
    {
        return FVector(SafeDimension(Value.X, Fallback.X), SafeDimension(Value.Y, Fallback.Y), SafeDimension(Value.Z, Fallback.Z));
    }
}

ADesertBuildingModule::ADesertBuildingModule()
{
    PrimaryActorTick.bCanEverTick = false;
#if WITH_EDITORONLY_DATA
    bRunConstructionScriptOnDrag = true;
#endif
    ModuleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModuleRoot"));
    SetRootComponent(ModuleRoot);
    ModuleRoot->SetMobility(EComponentMobility::Movable);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    DefaultBaseMaterial = Material.Object;
    auto CreateParts = [this](FName Name, UStaticMesh* Mesh)
    {
        UInstancedStaticMeshComponent* Parts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Parts->SetupAttachment(ModuleRoot);
        Parts->SetMobility(EComponentMobility::Movable);
        Parts->SetStaticMesh(Mesh);
        Parts->SetCollisionResponseToAllChannels(ECR_Block);
        Parts->SetCollisionObjectType(ECC_WorldStatic);
        Parts->SetGenerateOverlapEvents(false);
        Parts->SetCanEverAffectNavigation(false);
        return Parts;
    };
    WallBoxes = CreateParts(TEXT("WallBoxes"), Cube.Object);
    TrimBoxes = CreateParts(TEXT("TrimBoxes"), Cube.Object);
    WoodBoxes = CreateParts(TEXT("WoodBoxes"), Cube.Object);
    ClothBoxes = CreateParts(TEXT("ClothBoxes"), Cube.Object);
    DarkBoxes = CreateParts(TEXT("DarkBoxes"), Cube.Object);
    WallCylinders = CreateParts(TEXT("WallCylinders"), Cylinder.Object);
    PotCylinders = CreateParts(TEXT("PotCylinders"), Cylinder.Object);
    WoodCylinders = CreateParts(TEXT("WoodCylinders"), Cylinder.Object);
    DarkCylinders = CreateParts(TEXT("DarkCylinders"), Cylinder.Object);
    WallSpheres = CreateParts(TEXT("WallSpheres"), Sphere.Object);
    PotSpheres = CreateParts(TEXT("PotSpheres"), Sphere.Object);
    CustomModule = CreateParts(TEXT("CustomModule"), nullptr);
}

TArray<UInstancedStaticMeshComponent*> ADesertBuildingModule::GetModuleComponents() const
{
    return {WallBoxes.Get(), TrimBoxes.Get(), WoodBoxes.Get(), ClothBoxes.Get(), DarkBoxes.Get(),
        WallCylinders.Get(), PotCylinders.Get(), WoodCylinders.Get(), DarkCylinders.Get(),
        WallSpheres.Get(), PotSpheres.Get(), CustomModule.Get()};
}

FVector ADesertBuildingModule::GetKindDimensions() const
{
    return GetDefaultDimensions(ModuleKind);
}

FVector ADesertBuildingModule::GetDefaultDimensions(EDesertModuleKind Kind)
{
    switch (Kind)
    {
    case EDesertModuleKind::Stairs: return FVector(120, 450, 300);
    case EDesertModuleKind::RoofPavilion: return FVector(280, 280, 220);
    case EDesertModuleKind::Dome: return FVector(280, 280, 180);
    case EDesertModuleKind::Pot: return FVector(55, 55, 80);
    case EDesertModuleKind::Basket: return FVector(65, 65, 48);
    case EDesertModuleKind::Crate: return FVector(70, 60, 60);
    case EDesertModuleKind::PotCluster: return FVector(140, 110, 85);
    case EDesertModuleKind::AwningBay: return FVector(280, 200, 240);
    case EDesertModuleKind::WallStairs: return FVector(600, 120, 300);
    case EDesertModuleKind::SwitchbackStairs: return FVector(300, 400, 300);
    case EDesertModuleKind::RubbleCluster: return FVector(220, 160, 65);
    case EDesertModuleKind::RoofCrown: return FVector(300, 300, 180);
    case EDesertModuleKind::LShapeStairs: return FVector(510,510,300);
    case EDesertModuleKind::UShapeStairs: return FVector(300,480,300);
    default: return FVector(60, 60, 300);
    }
}

void ADesertBuildingModule::ApplyKindDefaults()
{
    Modify();
    Dimensions = GetKindDimensions();
    ReferenceDimensions = Dimensions;
    StepsCount = ModuleKind == EDesertModuleKind::SwitchbackStairs || ModuleKind==EDesertModuleKind::LShapeStairs || ModuleKind==EDesertModuleKind::UShapeStairs ? 16 : 15;
    bClusterBase = false;
    bAwningProps = false;
    bCollision = ModuleKind == EDesertModuleKind::Column || ModuleKind == EDesertModuleKind::Stairs ||
        ModuleKind == EDesertModuleKind::WallStairs || ModuleKind == EDesertModuleKind::SwitchbackStairs || ModuleKind == EDesertModuleKind::RoofCrown || ModuleKind==EDesertModuleKind::LShapeStairs || ModuleKind==EDesertModuleKind::UShapeStairs;
    bAffectNavigation = false;
    Rebuild();
}

void ADesertBuildingModule::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // 关卡保存编辑阶段已经生成的实例。进入游戏直接展示这些实例，无需开局再次生成。
    if (GetWorld() && !GetWorld()->IsGameWorld()) Rebuild();
}

#if WITH_EDITOR
void ADesertBuildingModule::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    // 从下拉框改类型时立即得到适合该类型的尺寸；再手调尺寸不会被每次构造覆盖。
    if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ADesertBuildingModule, ModuleKind))
    {
        ApplyKindDefaults();
    }
    Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ADesertBuildingModule::PostEditUndo()
{
    Super::PostEditUndo();
    if (GetWorld() && !GetWorld()->IsGameWorld()) Rebuild();
}
#endif

UStaticMesh* ADesertBuildingModule::ResolveMesh() const
{
    return MeshOverride ? MeshOverride.Get() : GetStyleMesh(Style.Get(), ModuleKind);
}

UStaticMesh* ADesertBuildingModule::GetStyleMesh(const UDesertBuildingStyle* InStyle, EDesertModuleKind Kind)
{
    if (!InStyle) return nullptr;
    switch (Kind)
    {
    case EDesertModuleKind::Column: return InStyle->ModuleColumn.Get();
    case EDesertModuleKind::Stairs: return InStyle->ModuleStairs.Get();
    case EDesertModuleKind::RoofPavilion: return InStyle->ModuleRoofPavilion.Get();
    case EDesertModuleKind::Dome: return InStyle->ModuleDome.Get();
    case EDesertModuleKind::Pot: return InStyle->ModulePot.Get();
    case EDesertModuleKind::Basket: return InStyle->ModuleBasket.Get();
    case EDesertModuleKind::Crate: return InStyle->ModuleCrate.Get();
    case EDesertModuleKind::PotCluster: return InStyle->ModulePotCluster.Get();
    case EDesertModuleKind::AwningBay: return InStyle->ModuleAwningBay.Get();
    case EDesertModuleKind::WallStairs: return InStyle->ModuleWallStairs.Get();
    case EDesertModuleKind::SwitchbackStairs: return InStyle->ModuleSwitchbackStairs.Get();
    case EDesertModuleKind::RubbleCluster: return InStyle->ModuleRubbleCluster.Get();
    case EDesertModuleKind::RoofCrown: return InStyle->ModuleRoofCrown.Get();
    case EDesertModuleKind::LShapeStairs: return InStyle->ModuleLShapeStairs.Get();
    case EDesertModuleKind::UShapeStairs: return InStyle->ModuleUShapeStairs.Get();
    default: return nullptr;
    }
}

void ADesertBuildingModule::ConfigureComponents()
{
    RestoreFadeMaterials();
    for (UInstancedStaticMeshComponent* Parts : GetModuleComponents())
    {
        Parts->ClearInstances();
        Parts->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        Parts->SetCollisionResponseToAllChannels(ECR_Block);
        Parts->SetCanEverAffectNavigation(bCollision && bAffectNavigation);
    }
    // 这里必须分配可保存的材质资产。没有Style时用引擎白材质，不在构造阶段制造临时MID。
    UMaterialInterface* Wall = Style && Style->WallMaterial ? Style->WallMaterial.Get() : DefaultBaseMaterial.Get();
    UMaterialInterface* Trim = Style && Style->TrimMaterial ? Style->TrimMaterial.Get() : DefaultBaseMaterial.Get();
    UMaterialInterface* Wood = Style && Style->WoodMaterial ? Style->WoodMaterial.Get() : DefaultBaseMaterial.Get();
    UMaterialInterface* Dark = Style && Style->DarkMaterial ? Style->DarkMaterial.Get() : DefaultBaseMaterial.Get();
    UMaterialInterface* Cloth = Style && Style->ClothMaterial ? Style->ClothMaterial.Get() : DefaultBaseMaterial.Get();
    UMaterialInterface* Pot = Style && Style->PotMaterial ? Style->PotMaterial.Get() : DefaultBaseMaterial.Get();
    WallBoxes->SetMaterial(0, Wall);
    WallCylinders->SetMaterial(0, Wall);
    WallSpheres->SetMaterial(0, Trim);
    TrimBoxes->SetMaterial(0, Trim);
    WoodBoxes->SetMaterial(0, Wood);
    WoodCylinders->SetMaterial(0, Wood);
    DarkBoxes->SetMaterial(0, Dark);
    DarkCylinders->SetMaterial(0, Dark);
    ClothBoxes->SetMaterial(0, Cloth);
    PotCylinders->SetMaterial(0, Pot);
    PotSpheres->SetMaterial(0, Pot);
    // 不把白模材质强行分配给正式整模块，多材质槽由资产自己管理。
    CustomModule->EmptyOverrideMaterials();
    CustomModule->SetStaticMesh(ResolveMesh());
}

namespace DesertModuleRules
{
// 同一套纯配方供建筑规则渲染器与独立预览Actor复用，所有Transform均为块的本地坐标。
struct FDesertRecipeBuilder
{
    FVector Dimensions;
    int32 StepsCount;
    bool bAwningProps;
    bool bClusterBase;
    TArray<FDesertModulePart>& Parts;
    enum : int32 { WallBoxes=0, TrimBoxes=1, WoodBoxes=2, ClothBoxes=3, DarkBoxes=4,
        WallCylinders=6, PotCylinders=11, WoodCylinders=8, DarkCylinders=10,
        WallSpheres=13, PotSpheres=17 };

    void AddShape(int32 Bucket, const FVector& Center, const FVector& Size, const FQuat& Rotation=FQuat::Identity)
    {
        FDesertModulePart& Part=Parts.AddDefaulted_GetRef();
        Part.Shape=Bucket/6;
        Part.MaterialRole=Bucket%6;
        // 引擎Cube/Cylinder/Sphere均使用100厘米参考尺寸。
        Part.Transform=FTransform(Rotation,Center,Size/100.0);
    }

void AddBeam(const FVector& From, const FVector& To, float Thickness)
{
    const FVector Delta = To - From;
    AddShape(WoodBoxes, (From + To) * 0.5, FVector(Thickness, Thickness, Delta.Size()),
        FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal()));
}

void BuildColumn()
{
    const FVector D = Dimensions;
    // 5件：底座、下收口、柱身、上收口、柱头。改变高度不会随机改变轮廓。
    AddShape(TrimBoxes, FVector(0,0,D.Z*0.04), FVector(D.X,D.Y,D.Z*0.08));
    AddShape(TrimBoxes, FVector(0,0,D.Z*0.11), FVector(D.X*0.78,D.Y*0.78,D.Z*0.06));
    AddShape(WallCylinders, FVector(0,0,D.Z*0.5), FVector(D.X*0.60,D.Y*0.60,D.Z*0.72));
    AddShape(TrimBoxes, FVector(0,0,D.Z*0.89), FVector(D.X*0.78,D.Y*0.78,D.Z*0.06));
    AddShape(TrimBoxes, FVector(0,0,D.Z*0.96), FVector(D.X,D.Y,D.Z*0.08));
}

void BuildStairs()
{
    // 每一级都是从地面长到该级顶端的实心盒：无薄片碰撞、无悬空踏步。
    // 枢轴Y=0是最低级前沿；最高级后沿Y=Dimensions.Y，顶面Z=Dimensions.Z。
    const double Run = Dimensions.Y / StepsCount;
    const double Rise = Dimensions.Z / StepsCount;
    for (int32 Index = 0; Index < StepsCount; ++Index)
    {
        const double Height = (Index + 1) * Rise;
        AddShape(WallBoxes, FVector(0,(Index+0.5)*Run,Height*0.5), FVector(Dimensions.X,Run,Height));
    }
}

void BuildWallStairs()
{
    // 一个完整组合：60厘米底平台 + 420厘米贴墙梯 + 120厘米顶部转角平台。
    // 以屋顶出口中心为X=0，外墙线为Y=0；所有几何均留在外墙外侧-Y。
    const double Length=Dimensions.X, Width=Dimensions.Y, Height=Dimensions.Z;
    const double BottomDepth=Length*0.10, TopDepth=Length*0.20, Run=Length*0.70;
    const double FlightEnd=-TopDepth*0.5;
    const double FlightStart=FlightEnd-Run;
    const double Rise=Height/StepsCount, Tread=Run/StepsCount;
    // 低平台12厘米厚（随层高缩放），第一踏步仍在Rise高；不会引入更高的首级障碍。
    const double BottomHeight=FMath::Min(Height*0.04,Rise*0.5);
    AddShape(TrimBoxes,FVector(FlightStart-BottomDepth*0.5,-Width*0.5,BottomHeight*0.5),
        FVector(BottomDepth,Width,BottomHeight));
    for(int32 I=0; I<StepsCount; ++I)
    {
        const double StepHeight=(I+1)*Rise;
        AddShape(WallBoxes,FVector(FlightStart+(I+0.5)*Tread,-Width*0.5,StepHeight*0.5),
            FVector(Tread,Width,StepHeight));
    }
    // 实心顶部平台承托转角。玩家到顶后转向+Y即可进入屋顶开口。
    AddShape(TrimBoxes,FVector(0,-Width*0.5,Height*0.5),FVector(TopDepth,Width,Height));
}

void BuildSwitchbackStairs()
{
    // 出口居中接外墙，不要求屋顶开口偏移。全块边界X[-.75W,.25W]、Y[-D,0]。
    // 左段向-Y，右段向+Y。默认300×400平面：每段200长，底/中/顶平台均100深。
    // 左侧入口避免被依附的外墙封住；默认16级，每段8×18.75高×25深。
    const double Width=Dimensions.X, Depth=Dimensions.Y, Height=Dimensions.Z;
    const double FlightWidth=Width*0.40, LandingDepth=Depth*0.25, Run=Depth*0.50;
    const int32 HalfSteps=StepsCount/2;
    const double Rise=Height/StepsCount, Tread=Run/HalfSteps;
    const double LeftX=-Width*0.50;
    for(int32 I=0; I<HalfSteps; ++I)
    {
        const double LowerHeight=(I+1)*Rise;
        AddShape(WallBoxes,FVector(LeftX,-LandingDepth-(I+0.5)*Tread,LowerHeight*0.5),
            FVector(FlightWidth,Tread,LowerHeight));
        const double UpperHeight=Height*0.5+(I+1)*Rise;
        AddShape(WallBoxes,FVector(0,-LandingDepth-Run+(I+0.5)*Tread,UpperHeight*0.5),
            FVector(FlightWidth,Tread,UpperHeight));
    }
    // 三块平台厚15厘米。底平台位于地面以下[-15,0]，不会在入口额外增加一级。
    constexpr double PlatformThickness=15.0;
    AddShape(TrimBoxes,FVector(LeftX,-LandingDepth*0.5,-PlatformThickness*0.5),
        FVector(Width*0.50,LandingDepth,PlatformThickness));
    AddShape(TrimBoxes,FVector(-Width*0.25,-Depth+LandingDepth*0.5,Height*0.5-PlatformThickness*0.5),
        FVector(Width,LandingDepth,PlatformThickness));
    AddShape(TrimBoxes,FVector(0,-LandingDepth*0.5,Height-PlatformThickness*0.5),
        FVector(Width*0.50,LandingDepth,PlatformThickness));
}

void BuildArt08Stairs(bool bLShape)
{
    const FVector Reference=bLShape ? FVector(510,510,300) : FVector(300,480,300);
    const FVector Scale=Dimensions/Reference;
    auto Box=[this,&Scale](FVector Min,FVector Max,int32 Role)
    { AddShape(Role,(Min+Max)*.5*Scale,(Max-Min)*Scale); };
    const int32 Half=StepsCount/2;
    const double Rise=300.0/StepsCount;
    if (bLShape)
    {
        Box(FVector(-435,-510,-15),FVector(-315,-360,0),TrimBoxes);
        for (int32 I=0;I<Half;++I)
        {
            const double Tread=240.0/Half;
            Box(FVector(-315+I*Tread,-510,0),FVector(-315+(I+1)*Tread,-360,(I+1)*Rise),WallBoxes);
            Box(FVector(-75,-360+I*Tread,0),FVector(75,-360+(I+1)*Tread,150+(I+1)*Rise),WallBoxes);
        }
        Box(FVector(-75,-510,0),FVector(75,-360,150),TrimBoxes);
        Box(FVector(-75,-120,0),FVector(75,0,300),TrimBoxes);
    }
    else
    {
        Box(FVector(-225,-120,-15),FVector(-75,0,0),TrimBoxes);
        for (int32 I=0;I<Half;++I)
        {
            const double Tread=240.0/Half;
            Box(FVector(-225,-120-(I+1)*Tread,0),FVector(-75,-120-I*Tread,(I+1)*Rise),WallBoxes);
            Box(FVector(-75,-360+I*Tread,0),FVector(75,-360+(I+1)*Tread,150+(I+1)*Rise),WallBoxes);
        }
        Box(FVector(-225,-480,0),FVector(75,-360,150),TrimBoxes);
        Box(FVector(-75,-120,0),FVector(75,0,300),TrimBoxes);
    }
}

void BuildRoofPavilion()
{
    const double W = Dimensions.X, D = Dimensions.Y, H = Dimensions.Z;
    const double Post = FMath::Min(W,D) * 0.055;
    const double Roof = H * 0.035;
    const double Beam = H * 0.065;
    const double X = W*0.5-Post, Y = D*0.5-Post;
    for (double SignX : {-1.0,1.0})
    {
        for (double SignY : {-1.0,1.0})
        {
            AddShape(WoodBoxes, FVector(SignX*X,SignY*Y,(H-Roof)*0.5), FVector(Post,Post,H-Roof));
            const FVector Top(SignX*X,SignY*Y,H-Roof-Beam);
            AddBeam(Top-FVector(0,0,H*0.22), Top-FVector(SignX*W*0.18,0,0), Post*0.60f);
            AddBeam(Top-FVector(0,0,H*0.22), Top-FVector(0,SignY*D*0.18,0), Post*0.60f);
        }
    }
    for (double Sign : {-1.0,1.0})
    {
        AddShape(WoodBoxes,FVector(0,Sign*Y,H-Roof-Beam*0.5),FVector(W,Post,Beam));
        AddShape(WoodBoxes,FVector(Sign*X,0,H-Roof-Beam*0.5),FVector(Post,D,Beam));
    }
    AddShape(ClothBoxes,FVector(0,0,H-Roof*0.5),FVector(W,D,Roof));
}

void BuildDome()
{
    // 白模用扁球表现穹顶体量；正式半球或拱券通过整模块网格替换，不冒充精确半球。
    AddShape(TrimBoxes,FVector(0,0,Dimensions.Z*0.075),FVector(Dimensions.X,Dimensions.Y,Dimensions.Z*0.15));
    AddShape(WallBoxes,FVector(0,0,Dimensions.Z*0.19),FVector(Dimensions.X*0.9,Dimensions.Y*0.9,Dimensions.Z*0.08));
    AddShape(WallSpheres,FVector(0,0,Dimensions.Z*0.575),FVector(Dimensions.X*0.9,Dimensions.Y*0.9,Dimensions.Z*0.85));
}

void BuildPot()
{
    BuildPotShape(Dimensions, FVector::ZeroVector);
}

void BuildPotShape(const FVector& D, const FVector& Origin)
{
    AddShape(PotCylinders,Origin+FVector(0,0,D.Z*0.05),FVector(D.X*0.52,D.Y*0.52,D.Z*0.10));
    AddShape(PotSpheres,Origin+FVector(0,0,D.Z*0.40),FVector(D.X,D.Y,D.Z*0.70));
    AddShape(PotCylinders,Origin+FVector(0,0,D.Z*0.79),FVector(D.X*0.40,D.Y*0.40,D.Z*0.24));
    // 罐口由16个短圆润块围成；深色薄盘下沉在口沿之下，仅模拟暗部，不是完整内壁。
    for (int32 I=0; I<16; ++I)
    {
        const double Angle = 2.0*PI*I/16.0;
        AddShape(PotSpheres,Origin+FVector(FMath::Cos(Angle)*D.X*0.215,FMath::Sin(Angle)*D.Y*0.215,D.Z*0.95),
            FVector(D.X*0.11,D.Y*0.11,D.Z*0.10));
    }
    AddShape(DarkCylinders,Origin+FVector(0,0,D.Z*0.915),FVector(D.X*0.35,D.Y*0.35,D.Z*0.006));
}

void BuildPotCluster()
{
    BuildPotClusterShape(Dimensions, FVector::ZeroVector, bClusterBase);
}

void BuildPotClusterShape(const FVector& D, const FVector& Origin, bool bIncludeBase)
{
    // 固定疏密与大小层次。改别的块不会使这个组合随机换位置。
    const double BaseHeight = bIncludeBase ? D.Z*0.06 : 0.0;
    if (bIncludeBase) AddShape(TrimBoxes,Origin+FVector(0,0,BaseHeight*0.5),FVector(D.X,D.Y,BaseHeight));
    const double PotHeight = D.Z-BaseHeight;
    BuildPotShape(FVector(D.X*0.42,D.Y*0.50,PotHeight),Origin+FVector(-D.X*0.23,-D.Y*0.20,BaseHeight));
    BuildPotShape(FVector(D.X*0.34,D.Y*0.40,PotHeight*0.72),Origin+FVector(D.X*0.28,-D.Y*0.10,BaseHeight));
    BuildPotShape(FVector(D.X*0.28,D.Y*0.32,PotHeight*0.52),Origin+FVector(D.X*0.04,D.Y*0.30,BaseHeight));
}

void BuildAwningBay()
{
    // 一个组合块承载四柱、两条顺坡梁、两条横梁和篷布。前侧-Y较低，背侧+Y较高。
    const double W=Dimensions.X, D=Dimensions.Y, H=Dimensions.Z;
    const double Post=FMath::Min(W,D)*0.05, Roof=H*0.02, Beam=H*0.04;
    const double Back=H-Roof*0.5, Front=H*0.85-Roof*0.5;
    const double Drop=Back-Front, SlopedLength=FMath::Sqrt(D*D+Drop*Drop);
    const FQuat Slope(FVector::XAxisVector,FMath::Atan2(Drop,D));
    const FVector RoofCenter(0,0,(Back+Front)*0.5);
    const double X=W*0.5-Post, Y=D*0.5-Post;
    for (double SX:{-1.0,1.0})
    {
        for (double SY:{-1.0,1.0})
        {
            const double Height=(Back+Front)*0.5+Drop*SY*Y/D-Roof*0.5;
            AddShape(WoodBoxes,FVector(SX*X,SY*Y,Height*0.5),FVector(Post,Post,Height));
        }
        AddShape(WoodBoxes,RoofCenter+FVector(SX*X,0,-Roof*0.5-Beam*0.5),FVector(Post,SlopedLength,Beam),Slope);
    }
    for (double SY:{-1.0,1.0})
    {
        const double Height=(Back+Front)*0.5+Drop*SY*Y/D-Roof*0.5-Beam*0.5;
        AddShape(WoodBoxes,FVector(0,SY*Y,Height),FVector(W,Post,Beam));
    }
    AddShape(ClothBoxes,RoofCenter,FVector(W,SlopedLength,Roof),Slope);
    if (bAwningProps) BuildPotClusterShape(FVector(W*0.38,D*0.40,H*0.30),FVector(-W*0.20,D*0.12,0),false);
}

void BuildBasket()
{
    const FVector D = Dimensions;
    AddShape(WoodCylinders,FVector(0,0,D.Z*0.43),FVector(D.X*0.88,D.Y*0.88,D.Z*0.86));
    for (int32 I=0; I<16; ++I)
    {
        const double Angle = 2.0*PI*I/16.0;
        const FQuat Rotation(FVector::UpVector, Angle);
        // 两圈边箍和八根竖向编条；没有增加纹理或随机噪点，远看先读清形体。
        for (double Z : {0.18,0.94})
        {
            AddShape(WoodBoxes,FVector(FMath::Cos(Angle)*D.X*0.455,FMath::Sin(Angle)*D.Y*0.455,D.Z*Z),
                FVector(D.X*0.07,D.Y*0.19,D.Z*0.12),Rotation);
        }
        if (I%2==0)
        {
            AddShape(WoodBoxes,FVector(FMath::Cos(Angle)*D.X*0.45,FMath::Sin(Angle)*D.Y*0.45,D.Z*0.50),
                FVector(D.X*0.045,D.Y*0.06,D.Z*0.74),Rotation);
        }
    }
    AddShape(DarkCylinders,FVector(0,0,D.Z*0.875),FVector(D.X*0.82,D.Y*0.82,D.Z*0.008));
}

void BuildCrate()
{
    const FVector D=Dimensions;
    // 深色芯与分开的木板形成清晰缝隙，不需要提前准备纹理。
    AddShape(DarkBoxes,FVector(0,0,D.Z*0.48),FVector(D.X*0.93,D.Y*0.93,D.Z*0.94));
    for (int32 I=0; I<4; ++I)
    {
        const double X=(-0.375+I*0.25)*D.X;
        for (double Sign : {-1.0,1.0})
            AddShape(WoodBoxes,FVector(X,Sign*D.Y*0.475,D.Z*0.5),FVector(D.X*0.235,D.Y*0.05,D.Z));
        AddShape(WoodBoxes,FVector(X,0,D.Z*0.975),FVector(D.X*0.235,D.Y,D.Z*0.05));
    }
    for (int32 I=0; I<3; ++I)
        for (double Sign : {-1.0,1.0})
            AddShape(WoodBoxes,FVector(Sign*D.X*0.475,0,(I+0.5)*D.Z/3.0),FVector(D.X*0.05,D.Y*0.9,D.Z*0.31));
    for (double SX : {-1.0,1.0})
        for (double SY : {-1.0,1.0})
            AddShape(WoodBoxes,FVector(SX*D.X*0.43,SY*D.Y*0.43,D.Z*0.5),FVector(D.X*0.14,D.Y*0.14,D.Z));
}

 };
}

void ADesertBuildingModule::MakeVisualRecipe(EDesertModuleKind Kind, FVector Size, int32 Steps,
    bool bWithProps, bool bWithBase, TArray<FDesertModulePart>& OutParts)
{
    OutParts.Reset();
    if (static_cast<uint8>(Kind)>static_cast<uint8>(EDesertModuleKind::UShapeStairs)) Kind=EDesertModuleKind::Column;
    Size=DesertModuleRules::SanitizeDimensions(Size,GetDefaultDimensions(Kind));
    Steps=FMath::Clamp(Steps,1,128);
    if(Kind==EDesertModuleKind::SwitchbackStairs || Kind==EDesertModuleKind::LShapeStairs || Kind==EDesertModuleKind::UShapeStairs) Steps=FMath::Clamp(Steps+(Steps%2),2,128);
    DesertModuleRules::FDesertRecipeBuilder Builder {Size,Steps,bWithProps,bWithBase,OutParts};
    switch(Kind)
    {
    case EDesertModuleKind::Column: Builder.BuildColumn(); break;
    case EDesertModuleKind::Stairs: Builder.BuildStairs(); break;
    case EDesertModuleKind::RoofPavilion: Builder.BuildRoofPavilion(); break;
    case EDesertModuleKind::Dome: Builder.BuildDome(); break;
    case EDesertModuleKind::Pot: Builder.BuildPot(); break;
    case EDesertModuleKind::Basket: Builder.BuildBasket(); break;
    case EDesertModuleKind::Crate: Builder.BuildCrate(); break;
    case EDesertModuleKind::PotCluster: Builder.BuildPotCluster(); break;
    case EDesertModuleKind::AwningBay: Builder.BuildAwningBay(); break;
    case EDesertModuleKind::WallStairs: Builder.BuildWallStairs(); break;
    case EDesertModuleKind::SwitchbackStairs: Builder.BuildSwitchbackStairs(); break;
    case EDesertModuleKind::LShapeStairs: Builder.BuildArt08Stairs(true); break;
    case EDesertModuleKind::UShapeStairs: Builder.BuildArt08Stairs(false); break;
    case EDesertModuleKind::RubbleCluster:
        // 确定性组合白模：整组一个作者块。首版不做随机每石散布或地形适配。
        Builder.AddShape(0,FVector(-Size.X*.32,-Size.Y*.22,Size.Z*.35),FVector(Size.X*.36,Size.Y*.42,Size.Z*.70));
        Builder.AddShape(0,FVector(Size.X*.23,Size.Y*.27,Size.Z*.50),FVector(Size.X*.42,Size.Y*.46,Size.Z));
        Builder.AddShape(0,FVector(Size.X*.33,-Size.Y*.31,Size.Z*.18),FVector(Size.X*.20,Size.Y*.28,Size.Z*.36));
        Builder.AddShape(0,FVector(-Size.X*.18,Size.Y*.34,Size.Z*.16),FVector(Size.X*.22,Size.Y*.25,Size.Z*.32));
        break;
    case EDesertModuleKind::RoofCrown:
        // 墙冠由其专用实际网格提供，不能借用穹顶、棚亭或不对应的方盒配方。
        break;
    }
}

void ADesertBuildingModule::Rebuild()
{
    if (!WallBoxes) return;
    if (static_cast<uint8>(ModuleKind)>static_cast<uint8>(EDesertModuleKind::UShapeStairs)) ModuleKind=EDesertModuleKind::Column;
    Dimensions=DesertModuleRules::SanitizeDimensions(Dimensions,GetKindDimensions());
    ReferenceDimensions=DesertModuleRules::SanitizeDimensions(ReferenceDimensions,GetKindDimensions());
    StepsCount=FMath::Clamp(StepsCount,1,128);
    if(ModuleKind==EDesertModuleKind::SwitchbackStairs || ModuleKind==EDesertModuleKind::LShapeStairs || ModuleKind==EDesertModuleKind::UShapeStairs) StepsCount=FMath::Clamp(StepsCount+(StepsCount%2),2,128);
    ConfigureComponents();
    bUsingCustomMesh=CustomModule->GetStaticMesh()!=nullptr;
    if (bUsingCustomMesh)
    {
        CustomModule->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,Dimensions/ReferenceDimensions),false);
    }
    else
    {
        TArray<FDesertModulePart> Recipe;
        MakeVisualRecipe(ModuleKind,Dimensions,StepsCount,bAwningProps,bClusterBase,Recipe);
        UInstancedStaticMeshComponent* Buckets[18] = {
            WallBoxes.Get(),TrimBoxes.Get(),WoodBoxes.Get(),ClothBoxes.Get(),DarkBoxes.Get(),nullptr,
            WallCylinders.Get(),nullptr,WoodCylinders.Get(),nullptr,DarkCylinders.Get(),PotCylinders.Get(),
            nullptr,WallSpheres.Get(),nullptr,nullptr,nullptr,PotSpheres.Get()
        };
        for (const FDesertModulePart& Part : Recipe)
        {
            const int32 Index=Part.Shape*6+Part.MaterialRole;
            if (Index>=0 && Index<18 && Buckets[Index]) Buckets[Index]->AddInstance(Part.Transform,false);
        }
    }
    InstanceCount=CollisionInstanceCount=0;
    for(UInstancedStaticMeshComponent* Parts:GetModuleComponents())
    {
        InstanceCount+=Parts->GetInstanceCount();
        if(Parts->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) CollisionInstanceCount+=Parts->GetInstanceCount();
    }
    if (CurrentOcclusionFade < 1.0f) RefreshFadeMaterials();
}

void ADesertBuildingModule::RestoreFadeMaterials()
{
    for(const FDesertModuleFadeSlot& Slot:FadeSlots)
        if(Slot.Component && Slot.Component->GetMaterial(Slot.MaterialIndex)==Slot.DynamicMaterial)
            Slot.Component->SetMaterial(Slot.MaterialIndex,Slot.BaseMaterial);
    FadeSlots.Reset();
}

void ADesertBuildingModule::RefreshFadeMaterials()
{
    RestoreFadeMaterials();
    CachedOcclusionParameter=OcclusionFadeParameter;
    for(UInstancedStaticMeshComponent* Parts:GetModuleComponents())
    {
        if(!Parts->GetStaticMesh() || Parts->GetInstanceCount()==0) continue;
        for(int32 Index=0;Index<Parts->GetNumMaterials();++Index)
        {
            UMaterialInterface* Base=Parts->GetMaterial(Index);
            if(!Base) continue;
            UMaterialInstanceDynamic* Existing=Cast<UMaterialInstanceDynamic>(Base);
            UMaterialInterface* Parent=Existing ? Existing->Parent.Get():Base;
            if(!Parent) continue;
            UMaterialInstanceDynamic* Dynamic=UMaterialInstanceDynamic::Create(Parent,this);
            if(!Dynamic) continue;
            if(Existing) Dynamic->CopyParameterOverrides(Existing);
            Dynamic->SetScalarParameterValue(OcclusionFadeParameter,CurrentOcclusionFade);
            Parts->SetMaterial(Index,Dynamic);
            FDesertModuleFadeSlot& Slot=FadeSlots.AddDefaulted_GetRef();
            Slot.Component=Parts; Slot.BaseMaterial=Base; Slot.DynamicMaterial=Dynamic; Slot.MaterialIndex=Index;
        }
    }
}

void ADesertBuildingModule::SetOcclusionFade(float VisibleAmount)
{
    CurrentOcclusionFade=FMath::IsFinite(VisibleAmount)?FMath::Clamp(VisibleAmount,0.0f,1.0f):1.0f;
    // 结束预览时还原持久基材，保证保存/冻结的结果不会引用临时材质实例。
    if (CurrentOcclusionFade >= 1.0f) { RestoreFadeMaterials(); return; }
    bool bRefresh=FadeSlots.IsEmpty() || CachedOcclusionParameter!=OcclusionFadeParameter;
    for(const FDesertModuleFadeSlot& Slot:FadeSlots)
        if(!Slot.Component || !Slot.DynamicMaterial || Slot.Component->GetMaterial(Slot.MaterialIndex)!=Slot.DynamicMaterial) bRefresh=true;
    if(bRefresh) { RefreshFadeMaterials(); return; }
    for(const FDesertModuleFadeSlot& Slot:FadeSlots) Slot.DynamicMaterial->SetScalarParameterValue(OcclusionFadeParameter,CurrentOcclusionFade);
}
