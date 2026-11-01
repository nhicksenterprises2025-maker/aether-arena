"""Original Blade Box mechanism and reusable Thunderstorm presentation meshes."""
import math
import bpy
from mathutils import Vector

CHARACTERS = {'blade_box'}
STATICS = {'thunderstorm_cloud', 'thunderstorm_bolt', 'thunderstorm_rain', 'stun_spark'}
SPAWN_SECONDS = .60
OPEN_SECONDS = .16


def _blade_emblem(f, angle):
    center=Vector((0,-.789,1.714))
    def p(x,z,y=0):
        return center+Vector((x*math.cos(angle)+z*math.sin(angle),y,
                              z*math.cos(angle)-x*math.sin(angle)))
    outline=[(-.032,-.045),(-.037,.115),(0,.235),(.037,.115),(.032,-.045)]
    vertices=[p(x,z,.006) for x,z in outline]+[p(x,z,-.010) for x,z in outline]+[p(0,.090,-.029)]
    faces=[tuple(reversed(range(5)))]
    for n in range(5):
        faces.extend([(n,(n+1)%5,(n+1)%5+5,n+5),(5+n,5+(n+1)%5,10)])
    f.mesh_data('forged_blade_crest',vertices,faces,'silver','root',smooth=False)
    f.tube('crest_quillon',[p(-.066,-.052,-.004),p(.066,-.052,-.004)],.012,'brass','root',10)
    f.tube('crest_leather_grip',[p(0,-.061),p(0,-.139)],.016,'leather','root',12)
    f.ellipsoid('crest_pommel',p(0,-.148),(.022,.014,.022),'steel','root',16)


def build(Forge):
    f=Forge('blade_box');f.family='mechanism'
    f.bone('root',(0,0,0),(0,0,.20))
    for side,x in (('l',-.184),('r',.184)):
        f.bone('door_'+side,(x,-.685,.86),(x,-.685,1.10),'root')
    f.bone('latch',(0,.10,1.835),(0,.10,2.025),'root')
    f.bone('signal',(0,-.763,1.535),(0,-.763,1.605),'root')
    f.socket('spawn_origin',(0,-.90,.12),'root')
    f.socket('attack_origin',(0,-.90,.12),'root')
    f.socket('hp_anchor',(0,0,2.15),'root')
    f.socket('status_anchor',(0,0,1.96),'root')
    f.socket('impact_origin',(0,-.10,.90),'root')
    # A hollow, usable interior: rear and side walls leave the entrance clear.
    f.panel('dressed_foundation',(0,0,.095),(1.64,1.48,.19),'stone_dark','root',bevel=.055)
    f.panel('upper_foundation',(0,0,.205),(1.51,1.37,.07),'iron','root',bevel=.025)
    f.panel('interior_floor',(0,-.03,.247),(1.23,1.12,.016),'black','root',bevel=.009)
    f.panel('aether_floor_inlay',(0,-.10,.260),(.66,.65,.010),'cloth_purple','root',bevel=.015)
    f.panel('rear_wall',(0,.535,.90),(1.32,.12,1.31),'cloth_purple','root',bevel=.028)
    for sign in (-1,1):
        f.panel('side_wall',(sign*.63,-.015,.90),(.11,1.13,1.31),'cloth_purple','root',bevel=.027)
        for y in (-.575,.575):
            f.panel('corner_upright',(sign*.685,y,.89),(.145,.145,1.36),'steel_shadow','root',bevel=.022)
            f.panel('corner_highlight',(sign*.726,y-.04,.90),(.030,.065,1.22),'steel','root',bevel=.008)
            for z in (.35,1.42): f.rivet((sign*.687,y-.083,z),'root','brass',.021)
        f.panel('vent_recess',(sign*.691,.02,1.03),(.014,.43,.29),'black','root',bevel=.018)
        for n in range(5):
            f.panel('vent_louver',(sign*.707,.02,.92+n*.052),(.028,.39,.018),'steel','root',bevel=.004)
        f.panel('side_lower_binding',(sign*.697,-.02,.43),(.035,1.02,.075),'iron','root',bevel=.012)
        for y in (-.42,.42): f.rivet((sign*.722,y,.43),'root','brass',.017)
    # Front cheeks are behind the sliding door plane, creating real side pockets.
    for sign in (-1,1):
        f.panel('front_pocket',(sign*.523,-.570,.88),(.293,.105,1.27),'iron','root',bevel=.019)
        f.panel('pocket_purple_inset',(sign*.544,-.630,.88),(.19,.012,1.12),'cloth_purple','root',bevel=.010)
        f.panel('jamb',(sign*.377,-.634,.88),(.048,.055,1.29),'steel','root',bevel=.009)
    f.panel('entry_lintel',(0,-.623,1.545),(.82,.12,.11),'steel_shadow','root',bevel=.018)
    f.panel('upper_slide_rail',(0,-.727,1.485),(1.40,.035,.045),'silver','root',bevel=.009)
    f.panel('lower_slide_rail',(0,-.728,.279),(1.40,.035,.034),'steel','root',bevel=.007)
    for sign,side in ((-1,'l'),(1,'r')):
        bone='door_'+side;x=sign*.184
        f.panel('sliding_door',(x,-.685,.871),(.357,.065,1.153),'steel_shadow',bone,bevel=.016)
        f.panel('door_purple_field',(x,-.724,.867),(.282,.015,1.045),'cloth_purple',bone,bevel=.014)
        for dx in (-.145,.145):
            f.panel('door_edge_rebate',(x+dx,-.738,.871),(.021,.021,1.105),'steel',bone,bevel=.005)
        for z in (.35,1.39):
            f.panel('door_cross_strap',(x,-.741,z),(.304,.020,.040),'iron',bone,bevel=.007)
            for dx in (-.116,.116):f.rivet((x+dx,-.754,z),bone,'brass',.010)
        for z in (.305,1.447):
            f.tube('door_roller_axle',[(x,-.703,z),(x,-.741,z)],.015,'iron',bone,12)
            f.ring('door_rail_roller',(x,-.747,z),.028,.008,'steel',bone,axis='Y')
        f.tube('recessed_pull_handle',[(x-sign*.045,-.747,.80),(x-sign*.045,-.770,.82),
               (x-sign*.045,-.770,.93),(x-sign*.045,-.747,.95)],.011,'brass',bone,12)
        # Small paired chevrons make the opening direction legible.
        f.tube('door_direction_inlay',[(x+sign*.03,-.739,1.11),(x-sign*.035,-.739,1.16),
               (x+sign*.03,-.739,1.21)],.007,'silver',bone,8)
    # Gabled roof panels and seams form a sheltered assassin barracks silhouette.
    for sign in (-1,1):
        f.panel('pitched_roof',(sign*.385,.0,1.657),(.818,1.39,.07),'cloth_purple','root',
                rotation=(0,sign*.32,0),bevel=.018)
        for y in (-.66,-.28,.28,.66):
            f.tube('roof_reinforcing_seam',[(sign*.025,y,1.814),(sign*.765,y,1.568)],
                   [.016,.021],'steel_shadow','root',12)
        for y in (-.67,.67):f.rivet((sign*.73,y,1.59),'root','brass',.019)
    f.tube('roof_ridge_cap',[(0,-.72,1.814),(0,.72,1.814)],.045,'steel','root',16)
    f.panel('front_crest_backing',(0,-.754,1.702),(.405,.058,.287),'steel_shadow','root',bevel=.035)
    _blade_emblem(f,-.63);_blade_emblem(f,.63)
    # A hinged release lever and one clear status gem respond to the hatch.
    f.tube('latch_pivot',[(0,.10,1.808),(0,.10,1.852)],.074,'iron','root',20)
    f.panel('roof_release_lever',(0,.10,1.885),(.24,.062,.040),'brass','latch',bevel=.012)
    for x in (-.095,.095):f.ellipsoid('lever_end',(x,.10,1.885),(.034,.039,.030),'steel','latch',20)
    f.ellipsoid('spawn_signal_housing',(0,-.764,1.535),(.056,.022,.038),'iron','root',24)
    f.ellipsoid('spawn_signal_crystal',(0,-.790,1.535),(.035,.009,.020),'violet','signal',24,True)
    f.expansion_design={'revision':'2.1.0','design':'Hollow purple assassin barracks; fitted twin sliding doors, recessed handles, real rail rollers and forged blade crest',
        'animationContract':{'spawnSeconds':SPAWN_SECONDS,'openAtSeconds':OPEN_SECONDS,'closeStartsSeconds':.38,
            'spawnContactNormalized':OPEN_SECONDS/SPAWN_SECONDS,'doorTravelMeters':.35},
        'gameplayOwnership':'Presentation only; simulation owns every three-second spawn, unit count and lifetime'}
    return f.finish(True)


def actions(f,scene):
    durations={'Idle':2.,'Locomotion':2.,'Attack':SPAWN_SECONDS,'Spawn':SPAWN_SECONDS,
               'Hit':.18,'Death':.8,'Deploy':.45,'Status':1.,'Turn':.2,'Acquire':.2}
    result=[];fps=100
    def smooth(t):
        t=max(0,min(1,t));return t*t*(3-2*t)
    def move(name,offset):
        pb=f.rig.pose.bones[name]
        pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector(offset)
    for clip,duration in durations.items():
        action=bpy.data.actions.new('AN_blade_box_'+clip);f.rig.animation_data_create();f.rig.animation_data.action=action
        length=round(duration*fps)+1
        for pb in f.rig.pose.bones:pb.rotation_mode='XYZ'
        for frame in range(1,length+1):
            scene.frame_set(frame);seconds=(frame-1)/fps;t=seconds/duration
            for pb in f.rig.pose.bones:pb.location=(0,0,0);pb.rotation_euler=(0,0,0);pb.scale=(1,1,1)
            opening=0
            if clip in ('Attack','Spawn'):
                opening=smooth(seconds/OPEN_SECONDS) if seconds<OPEN_SECONDS else 1-smooth((seconds-.38)/.22)
                f.rig.pose.bones['latch'].rotation_euler[1]=-.40*opening
                f.rig.pose.bones['signal'].scale=(1+.16*opening,)*3
            elif clip=='Deploy':
                move('root',(0,0,.10*(1-smooth(t))))
                opening=.45*(1-smooth(t))
            elif clip=='Hit':
                f.rig.pose.bones['root'].rotation_euler[0]=.025*math.sin(math.pi*t)
            elif clip=='Death':
                opening=smooth(t)*.34
                move('root',(0,0,-.12*smooth(t)))
                f.rig.pose.bones['root'].rotation_euler[0]=.06*smooth(t)
            else:
                phase=math.tau*t
                f.rig.pose.bones['signal'].scale=(1+.025*math.sin(phase),)*3
                f.rig.pose.bones['latch'].rotation_euler[1]=.010*math.sin(phase)
            move('door_l',(-.35*opening,0,0));move('door_r',(.35*opening,0,0))
            for pb in f.rig.pose.bones:
                for channel in ('rotation_euler','location','scale'):pb.keyframe_insert(channel,frame=frame,group=pb.name)
        curves=[c for layer in action.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
        for curve in curves:
            for point in curve.keyframe_points:point.interpolation='LINEAR'
        action.use_fake_user=True
        result.append({'name':clip,'action':action.name,'frames':length,'fps':fps,'seconds':duration,
            'releaseNormalized':OPEN_SECONDS/SPAWN_SECONDS if clip in ('Attack','Spawn') else None,
            'loop':clip in ('Idle','Locomotion','Status'),'strideMeters':None,'object':action})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.location=(0,0,0);pb.rotation_euler=(0,0,0);pb.scale=(1,1,1)
    return result


def effect_static(Forge,name):
    f=Forge(name)
    if name=='thunderstorm_cloud':
        # Overlapping authored lobes are welded into one cloud surface, not a
        # collection of visible intersecting spheres. Origin is cloud center.
        lobes=[((0,0,0),(1.0,.51,.22)),((-.64,.02,.09),(.47,.39,.29)),
            ((.65,.03,.10),(.46,.37,.30)),((0,.08,.27),(.48,.39,.35)),
            ((-.33,.07,.23),(.43,.38,.34)),((.34,.13,.22),(.40,.36,.30)),
            ((-.74,-.15,-.04),(.34,.33,.22)),((.68,-.16,-.04),(.37,.32,.23)),
            ((-.38,-.29,.0),(.36,.29,.26)),((.06,-.34,.07),(.37,.27,.29)),
            ((.40,-.27,.0),(.34,.29,.24)),((-.35,.33,.03),(.40,.29,.25)),
            ((.37,.30,.02),(.44,.28,.25))]
        for location,scale in lobes:f.ellipsoid('cloud_cumulus_lobe',location,scale,'raven',detail=32)
        bpy.ops.object.select_all(action='DESELECT')
        for part in f.parts:part.select_set(True)
        bpy.context.view_layer.objects.active=f.parts[0];bpy.ops.object.join()
        cloud=bpy.context.object;f.parts=[]
        remesh=cloud.modifiers.new('Continuous storm vapor','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.032;remesh.use_smooth_shade=True
        bpy.ops.object.modifier_apply(modifier=remesh.name)
        smooth=cloud.modifiers.new('Soft billowing transitions','SMOOTH');smooth.factor=.52;smooth.iterations=5
        bpy.ops.object.modifier_apply(modifier=smooth.name)
        for layer in list(cloud.data.uv_layers):cloud.data.uv_layers.remove(layer)
        cloud.name='continuous_sculpted_storm_cloud';f.add(cloud,'raven')
    elif name=='thunderstorm_bolt':
        points=[(0,0,0),(-.058,-.010,-.17),(.033,.010,-.31),(-.042,-.006,-.48),
                (.082,.012,-.62),(.014,-.012,-.79),(.050,0,-1.)]
        f.tube('lightning_primary_stroke',points,[.016,.020,.021,.019,.016,.011,.0015],'ivory',sides=8,glow=True)
        for index,points in enumerate(([(.030,.010,-.31),(.148,.015,-.39),(.122,.019,-.54),(.205,.012,-.68)],
                                      [(-.041,-.006,-.48),(-.145,-.014,-.56),(-.120,-.024,-.67),(-.185,-.012,-.80)])):
            f.tube('lightning_fork_'+str(index),points,[.010,.008,.006,.001],'cyan',sides=7,glow=True)
    elif name=='thunderstorm_rain':
        f.tube('rain_streak',[(0,0,0),(0,0,-.76),(0,0,-1)], [.003,.006,.001],'ice',sides=6,glow=True)
    elif name=='stun_spark':
        outline=[]
        for n in range(8):
            a=math.pi*n/4;r=.5 if n%2==0 else .115
            outline.append(Vector((math.cos(a)*r,math.sin(a)*r,0)))
        for scale,z,key in ((1.,0,'cyan'),(.80,.025,'ivory')):
            vertices=[p*scale+Vector((0,0,z)) for p in outline]+[Vector((0,0,z+.045)),Vector((0,0,z-.020))]
            faces=[]
            for n in range(8):faces.extend([(n,(n+1)%8,8),((n+1)%8,n,9)])
            f.mesh_data('four_point_stun_spark',vertices,faces,key,smooth=False,glow=True)
    else:raise ValueError('Unknown Thunderstorm static: '+name)
    return f.finish()
