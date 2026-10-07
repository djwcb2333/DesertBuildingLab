"""Render the optional public kit in a fresh Unreal Editor process.

Save/close your working editor first. Start your .uproject with
  -ExecutePythonScript=<repository>/Tools/render_example.py -unattended -nosound
This creates an unsaved inspection level and writes two PNGs under Docs/Images.
No Unreal asset or map is saved. These are RHI renders, not plugin UI captures.
"""
from pathlib import Path
from datetime import datetime,timezone
import json,traceback,unreal

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'Docs/Images'
REPORT=ROOT/'Docs/render_example_report.json'
A=unreal.EditorAssetLibrary
E=unreal.MaterialEditingLibrary
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
r={'success':False,'utc':datetime.now(timezone.utc).isoformat(),'asset_or_map_saved':False,
   'method':'Actual UE SceneCapture RHI with the optional authored-wood kit; not plugin UI screenshots.','checks':[]}
def check(name,ok,actual=None):
    r['checks'].append({'name':name,'passed':bool(ok),'actual':actual})
    if not ok:raise AssertionError(name+': '+str(actual))
def write():REPORT.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
def block(kind,cell,facing=0,stair=None,variant=-1):
    b=unreal.DesertBlockPlacement();b.type=kind;b.cell=unreal.IntVector(*cell);b.facing=facing;b.variant_index=variant
    if stair is not None:b.stair_layout=stair
    if kind==unreal.DesertBlockType.POT_CLUSTER:b.pot_placement=unreal.DesertPotPlacement.AUTOMATIC
    return b
def room(cell,door,windows=15):
    b=unreal.DesertRoomAppearance();b.cell=unreal.IntVector(*cell);b.door_mode=door;b.override_windows=True;b.window_mask=windows;return b
def building(label,x,cells,blocks,overrides):
    b=actors.spawn_actor_from_class(unreal.DesertBuilding,unreal.Vector(x,0,0));b.set_actor_label(label)
    for k,v in [('style',style),('cells',[unreal.IntVector(*c) for c in cells]),('blocks',blocks),
                ('room_appearance_overrides',overrides),('enable_awning',False),('show_wood_beams',False),
                ('auto_support_columns',False),('seed',0)]:b.set_editor_property(k,v)
    b.rebuild()
    check(label+' valid rooms',b.cell_count==len(cells),b.cell_count)
    check(label+' all combination blocks accepted',b.invalid_block_count==0 and b.valid_block_count==len(blocks),list(b.validation_messages))
    return b
try:
    check('use fresh command-line editor process','ExecutePythonScript' in unreal.SystemLibrary.get_command_line())
    style=A.load_asset('/Game/DesertBuildingLabOpenSource/Buildings/ExampleHouse/Styles/DA_ExampleHouse_Style');assert style,'Install Examples/Content first.'
    assert unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    OUT.mkdir(parents=True,exist_ok=True)
    ground=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(5000,0,-25))
    ground.static_mesh_component.set_static_mesh(A.load_asset('/Engine/BasicShapes/Cube'))
    ground.set_actor_scale3d(unreal.Vector(240,120,.5))
    ground.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    ground.static_mesh_component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_BLOCK)
    K=unreal.DesertBlockType;S=unreal.DesertStairLayout;D=unreal.DesertRoomDoorMode
    hero=building('PublicKit_CombinationExample',0,[(0,0,0),(1,0,0),(1,0,1)],
       [block(K.STAIRS,(0,0,1),2,S.U_SHAPE),block(K.ROOF_PAVILION,(0,0,1)),
        block(K.AWNING_BAY,(2,0,0),1,variant=2),block(K.ROOF_CROWN,(1,0,2),2),
        block(K.POT_CLUSTER,(1,-1,0)),block(K.RUBBLE_CLUSTER,(0,-1,0))],
       [room((0,0,0),D.NO_DOOR,5),room((1,0,0),D.RIGHT,4),room((1,0,1),D.NO_DOOR,6)])
    for i in range(4):
        building('PublicKit_Canopy'+str(i),5000+i*700,[(0,0,0)],
                 [block(K.AWNING_BAY,(0,1,0),2,variant=i)],[room((0,0,0),D.BACK,0)])
    # Plain inspection floor uses a transient authored material, never a project/vendor material.
    sand=unreal.new_object(unreal.Material)
    color=E.create_material_expression(sand,unreal.MaterialExpressionConstant3Vector)
    color.constant=unreal.LinearColor(.31,.225,.13,1)
    rough=E.create_material_expression(sand,unreal.MaterialExpressionConstant);rough.r=.95
    E.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    E.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    E.recompile_material(sand);ground.static_mesh_component.set_material(0,sand)
    sun=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1800),unreal.Rotator(pitch=-40,yaw=-35,roll=0))
    lc=sun.get_component_by_class(unreal.DirectionalLightComponent);lc.set_mobility(unreal.ComponentMobility.MOVABLE);lc.set_intensity(5)
    for x in (0,5000,5700,6400,7100):
        fill=actors.spawn_actor_from_class(unreal.PointLight,unreal.Vector(x+1100,1300,1150))
        fc=fill.get_component_by_class(unreal.PointLightComponent);fc.set_mobility(unreal.ComponentMobility.MOVABLE)
        fc.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);fc.set_intensity(24000)
        fc.set_editor_property('attenuation_radius',3000);fc.set_editor_property('source_radius',300);fc.set_cast_shadows(False)
    sky=actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,1400));sc=sky.get_component_by_class(unreal.SkyLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    daylight=A.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap');assert daylight
    sc.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sc.set_editor_property('cubemap',daylight);sc.set_intensity(1);sc.recapture_sky()
    camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector())
    cap=camera.get_component_by_class(unreal.SceneCaptureComponent2D)
    for k,v in [('capture_every_frame',False),('capture_on_movement',False),('always_persist_rendering_state',True),
                ('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)]:cap.set_editor_property(k,v)
    target=unreal.RenderingLibrary.create_render_target2d(world,1600,1200,unreal.TextureRenderTargetFormat.RTF_RGBA8);cap.texture_target=target
    pp=cap.post_process_settings
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),
                ('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False),
                ('override_auto_exposure_bias',True),('auto_exposure_bias',.45),('override_bloom_intensity',True),('bloom_intensity',0),
                ('ambient_cubemap',daylight),('override_ambient_cubemap_intensity',True),('ambient_cubemap_intensity',.6)]:pp.set_editor_property(k,v)
    cap.post_process_settings=pp
    for cmd in ('r.EyeAdaptationQuality 0','r.ScreenPercentage 100','r.TextureStreaming 0'):unreal.SystemLibrary.execute_console_command(world,cmd)
    views=[('public-kit-hero',(380,230,370),1700,(2000,2150,1500)),
           ('public-kit-canopies',(6200,180,150),3200,(6600,4500,2200))]
    state={'frame':0,'view':0,'handle':None,'busy':False}
    def tick(dt):
        if state['busy']:return
        state['busy']=True
        finished=False
        try:
            state['frame']+=1
            name,look,width,pos=views[state['view']]
            if state['frame']==1:
                camera.set_actor_location_and_rotation(unreal.Vector(*pos),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*pos),unreal.Vector(*look)),False,False);cap.ortho_width=width
            if state['frame']>=8:cap.capture_scene()
            if state['frame']<71:return
            filename=name+'.png';unreal.RenderingLibrary.export_render_target(world,target,str(OUT),filename)
            check(name+' RHI image exists',(OUT/filename).is_file())
            state['frame']=0;state['view']+=1
            if state['view']==len(views):r['success']=True;r.pop('pending',None);finished=True
        except Exception:r['exception']=traceback.format_exc();finished=True
        finally:
            state['busy']=False
            if finished:
                unreal.unregister_slate_post_tick_callback(state['handle']);cap.texture_target=None
                write();unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    r['pending']='Waiting for real RHI render frames';write()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state['handle']=unreal.register_slate_post_tick_callback(tick)
except Exception:r['exception']=traceback.format_exc();write();unreal.log_error(r['exception'])
