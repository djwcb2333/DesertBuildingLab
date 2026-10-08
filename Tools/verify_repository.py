"""Portable checks for the public repository, without Unreal or Blender.

Checks actual file hashes, relative documentation links, privacy markers and
the recorded example dependency graph. Does not claim engine/runtime testing.
"""
from pathlib import Path
import hashlib,json,re,sys

ROOT=Path(__file__).resolve().parent.parent
PRIVATE={'ArtProvenanceAudit.json','SourcePublicationAudit.json'}
SKIP={'.git','Binaries','Intermediate','Saved','DerivedDataCache','.private','__pycache__'}
checks=[]
def check(name,ok,actual=None):
    checks.append({'name':name,'passed':bool(ok),'actual':actual})
    if not ok:raise AssertionError(name+': '+str(actual))
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def target(relative):
    p=(ROOT/relative).resolve()
    check('manifest path stays in repository '+relative,p.is_relative_to(ROOT))
    return p
def files():
    return [p for p in ROOT.rglob('*') if p.is_file() and p.name not in PRIVATE and not any(x in SKIP for x in p.relative_to(ROOT).parts)]
try:
    for name in ('LICENSE','ASSET_LICENSE.md','README.md','DesertBuildingLab.uplugin'):
        check('required file '+name,(ROOT/name).is_file())
    descriptor=read(ROOT/'DesertBuildingLab.uplugin')
    check('plugin version',descriptor['VersionName']=='0.4.9-ui.1')
    check('both runtime and editor modules',set(m['Name'] for m in descriptor['Modules'])=={'DesertBuildingLab','DesertBuildingLabEditor'})
    verification=read(ROOT/'Docs/Verification_0.4.9-ui.1.json')
    check('verification matches current version',verification['version']==descriptor['VersionName'])
    recorded_sources={entry['path'] for entry in verification['source_inventory']}
    actual_sources={p.relative_to(ROOT).as_posix() for p in (ROOT/'Source').rglob('*') if p.is_file()}
    check('verified source inventory is complete',recorded_sources==actual_sources)
    for entry in verification['source_inventory']:
        p=target(entry['path'])
        check('published source matches verified live source '+entry['path'],p.is_file() and sha(p)==entry['sha256'])
    check('current and historical engine checks remain distinct',
          [(r['tested_version'],r['passed'],r['total']) for r in verification['recorded_engine_checks']]==
          [('0.4.9-ui.1',168,168),('0.4.8-stairs.1',250,250),('0.4.8-stairs.1',491,491),('0.4.8-stairs.1',128,128)]
          and not verification['historical_checks_rerun_on_049'])
    screenshots=read(ROOT/'Docs/Images/ui-screenshots.json')
    check('all screenshot records counted',screenshots['screenshot_count']==len(screenshots['images']))
    for entry in screenshots['images']:
        p=target('Docs/Images/'+entry['file'])
        check('actual UI screenshot hash '+entry['file'],p.is_file() and sha(p)==entry['sha256'])
    art=read(ROOT/'SourceArt/manifest.json')
    check('52 source models',len(art['models'])==52)
    check('31 source textures',len(art['textures'])==31)
    for entry in art['models']+art['textures']:
        p=target('SourceArt/'+entry['file'])
        check('source resource hash '+entry['file'],p.is_file() and sha(p)==entry['sha256'])
    for model in art['models']:
        check('model dimensions use centimeters '+model['name'],
              set(model['actual_bounds_cm'])=={'minimum_cm','maximum_cm','dimensions_cm'} and 'pivot_cm' in model)
    example=read(ROOT/'Examples/manifest.json')
    check('public example complete',example['success'] and example['resource_count']==103 and example['public_texture_count']==30 and example['authored_wood_replacements']==6)
    check('public example recorded closure stays local',all(p.startswith((example['virtual_root']+'/', '/Engine/','/Script/')) for p in example['dependencies']))
    for entry in example['files']:
        p=target(entry['path'])
        check('native example hash '+entry['path'],p.is_file() and sha(p)==entry['sha256'])
    all_files=files()
    check('no file needs GitHub large-file exception',all(p.stat().st_size < 100*1024*1024 for p in all_files))
    leaks=[];credentials=[];broken=[]
    privacy=re.compile(r'(?<![A-Za-z])[A-Za-z]:[/\\](?:Users[/\\]|Codex Workspace|Unreal Project|新建文件夹)')
    for p in all_files:
        data=p.read_bytes();relative=p.relative_to(ROOT).as_posix()
        for encoding in ('utf8','utf-16-le'):
            found=privacy.findall(data.decode(encoding,errors='ignore'))
            if found:leaks.append([relative,encoding])
        if p.suffix.lower() in ('.md','.py','.h','.cpp','.cs','.json','.uplugin','.yml'):
            text=data.decode('utf-8-sig')
            if re.search(r'(?:ghp|github_pat|sk-proj)_[A-Za-z0-9_]{25,}',text):credentials.append(relative)
            if p.suffix=='.md':
                for url in re.findall(r'\]\(([^)]+)\)',text):
                    url=url.strip('<>');url=url.split('#',1)[0]
                    if not url or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:',url):continue
                    if not (p.parent/url).resolve().is_file():broken.append([relative,url])
    check('no developer private folder markers',not leaks,leaks)
    check('no credential-like strings',not credentials,credentials)
    check('all local documentation links exist',not broken,broken)
    print(json.dumps({'success':True,'passed':len(checks),'file_count':len(all_files),
                      'bytes':sum(p.stat().st_size for p in all_files),
                      'scope':'Repository only; no engine execution.'},ensure_ascii=False,indent=2))
except Exception as error:
    print(json.dumps({'success':False,'passed':sum(x['passed'] for x in checks),'failures':[x for x in checks if not x['passed']],
                      'error':str(error)},ensure_ascii=False,indent=2));sys.exit(1)
