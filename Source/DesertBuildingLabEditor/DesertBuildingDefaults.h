#pragma once

#include "DesertBuildingStyle.h"

/** 插件资源为默认入口；旧教学项目资源只是可选回退，不成为插件依赖。 */
inline UDesertBuildingStyle* DesertLoadDefaultBuildingStyle()
{
    const TCHAR* Candidates[] = {
        TEXT("/DesertBuildingLab/Art09/Styles/DA_ART09_KitStyle.DA_ART09_KitStyle"),
        TEXT("/DesertBuildingLab/Art08/Styles/DA_ART08_KitStyle.DA_ART08_KitStyle"),
        TEXT("/DesertBuildingLab/Art07/Styles/DA_ART07_KitStyle.DA_ART07_KitStyle"),
        TEXT("/DesertBuildingLab/Art06/Styles/DA_ART06_KitStyle.DA_ART06_KitStyle"),
        TEXT("/DesertBuildingLab/Art05/Styles/DA_ART05_KitStyle.DA_ART05_KitStyle"),
        TEXT("/DesertBuildingLab/Styles/DA_DefaultDesertStyle.DA_DefaultDesertStyle"),
        TEXT("/Game/DesertPrototype/V2/Styles/DA_DesertBlocksSand.DA_DesertBlocksSand"),
        TEXT("/Game/DesertPrototype/Styles/DA_DesertSand.DA_DesertSand")
    };
    for(const TCHAR* Path:Candidates)
        if(UDesertBuildingStyle* Style=LoadObject<UDesertBuildingStyle>(nullptr,Path,nullptr,LOAD_NoWarn)) return Style;
    return nullptr;
}
