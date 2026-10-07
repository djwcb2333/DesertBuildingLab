# 可编辑的建筑源资源

这里包含 52 个正式模块 FBX 和 31 张 PNG。`manifest.json` 登记每个文件的尺寸、SHA、材质槽、Style 槽、碰撞和原运行资产映射。`verification.json` 是本次实际文件核查结果；它不代表 Unreal 的渲染测试或美术验收。

## 模型导入

FBX 保留了源模型的顶点、索引、法线、切线、UV、顶点色、材质槽、模型变换、UCX 和连接数据，只清除了制作电脑的文件路径。清理使用 Blender 内置 FBX 读写库完成，没有把模型重新构造为 Blender 网格，也没有降低模型精度。

在 Unreal 导入时启用场景/单位转换，关闭 **Import Materials** 和 **Import Textures**。由套件材质槽表分配实际材质。FBX 内的旧贴图文件名已清空，不能依赖自动查找贴图。

| 约定 | 要求 |
|---|---|
| 单位 | 发布的 FBX 已烘焙为厘米，`UnitScaleFactor=1`；不要再给模型乘 100。DCC 制作时的旧合同用米；标准房间格为 300 × 300 × 300 cm |
| 朝向 | 运行模块为 +X 右、-Y 外侧、+Z 上；FBX 是右手坐标，Unreal 的 Convert Scene 将 Y 转为相反符号。保留正常场景转换，不手动再镜像 |
| 原点 | 通常为底部中心；具体模块以 manifest 的原点和包围盒记录为准 |
| 接面 | 不改变两侧、顶底接口的平直保护区；外露墙角才使用柔化模型 |
| 法线与切线 | 导入保留源法线和切线，避免重算后改变修整好的光面 |
| UV0 | 木、布、陶罐等使用实际材质 UV；四种篷布是整张布片 UV，和旧布图集不同 |
| UV1 | 独立 PaintUV1；用于后续绘制/烘焙。不要把每个模块的 UV0 当成连续墙面的坐标 |
| 顶点色 | R 预留可刷尘土；G 外露凸边磨损；B 凹处老化；A 保持 1 |
| 碰撞 | 保留 `UCX_` 名称及数量；楼梯为可走支撑，不用整块包围盒封住门窗 |

门、窗本身都是实际几何开洞；窗没有玻璃或褐色封板。门洞保持 140 cm 宽、225 cm 高的样件合同，最终仍应按项目角色碰撞胶囊实测。

## 纹理导入

`Textures/Original` 是当前套件使用的自制贴图与一个明确标注的 AI 红金布颜色图。`Textures/AuthoredWood` 是六张独立绘制的木纹替代，公开版使用它们替代私有开发阶段的 Painter starter 通用木材输出。

| 通道 | Unreal 设置 |
|---|---|
| BaseColor | sRGB 开；BC7；颜色采样 |
| Normal | sRGB 关；Normalmap 压缩；DirectX 切线空间；**不要再翻绿色通道** |
| ORM | sRGB 关；Masks；R=AO、G=Roughness、B=Metallic |
| Roughness | sRGB 关；Masks；读取 R |
| Height | 16 位灰度技术源；线性导入。当前样件不默认启用位移或视差 |

普通表面使用 Wrap 和 Mipmap。1254 × 1254 的 AI 布颜色源不宣称原生 2K；Unreal 重采样仅用于 Mip 条件，不增加原图细节。织纹技术图为 2048 × 2048，描述微小织纹；布面的大片下垂、前沿垂布和主要褶皱已经存在于模型中。

灰泥与石材应通过统一建筑坐标采样，使相邻墙片的纹理连续。墙面的剥落参数属于材质，不需要为每种房间拼接组合重新画一张独占颜色贴图。尘土绘制与默认底材分开，便于编辑者后续手动控制。

## 校验与修改

安装 Blender 后，在仓库根目录运行：

```text
blender --background --factory-startup --python Tools/prepare_source_art_blender.py -- --check-only
```

它重新解析 52 个 FBX，并核对所有逻辑字段、几何统计、无绝对路径、单位/轴向/模型变换、实际运行厘米包围盒，以及 31 张纹理的 SHA/PNG 信息。清单的 `actual_bounds_cm` 使用 `minimum_cm`、`maximum_cm`、`dimensions_cm`；原点使用 `pivot_cm`。该命令只读检查源资源，不会打开或修改你在 Blender 中正在编辑的场景。

编辑资源后，旧 SHA 会失效；应更新 manifest 并在 Unreal 重新导入自己的资源副本。改变楼梯宽度、踏步、平台、出口位置或模块占用尺寸时，也要更新 Style 的规则参数。更换颜色贴图可只替换材质，改变模块轮廓则要同时检查接缝、碰撞和放置规则。
