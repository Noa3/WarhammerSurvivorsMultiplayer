"""Build the owned plugin and a fresh Unity IL2CPP player; never load Steam."""
from pathlib import Path
from collections import Counter
import hashlib, os, re, shutil, subprocess, sys, tempfile

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[1]
UNITY = Path('C:/Program Files/Unity/Hub/Editor/6000.0.74f1/Editor/Unity.exe')
EXPECTED = set('missing_metadata initialize steam_not_running uninitialized_handle initialized unicode_trim whitespace_only null_name malformed_utf16 oversized embedded_nul running_exception handle_exception conversion_exception persona_exception wrong_thread null_context repeated_gc wrong_os_init wrong_managed_init'.split())

def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()

def run():
    scratch = Path(os.environ.get('TMPDIR') or (ROOT/'.local/steam-persona-runs'))
    scratch.mkdir(parents=True,exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix='steam-persona-',dir=scratch))
    native=base/'native'; project=base/'project'; player=base/'player'; log=base/'player.log'; editorlog=base/'editor.log'
    print('EVIDENCE_DIR',base,flush=True)
    env=os.environ.copy();env['WSM_FIXTURE_OUTPUT']=str(player/'Fixture.exe')
    for key in ('TMPDIR','TMP','TEMP'): env[key]=str(scratch)
    def step(args,timeout=600):
        print('STEP',args[0],flush=True)
        subprocess.run(args,check=True,timeout=timeout,env=env,cwd=ROOT)
    step(['cmake','-S',str(SOURCE/'Native'),'-B',str(native),'-G','Visual Studio 18 2026','-A','x64'],120)
    step(['cmake','--build',str(native),'--config','Release'],180)
    for folder in ('Assets','Packages','ProjectSettings'):shutil.copytree(SOURCE/folder,project/folder)
    plugin=native/'Release/steam_fixture.dll'; destination=project/'Assets/Plugins/x86_64/steam_fixture.dll';destination.parent.mkdir(parents=True);shutil.copyfile(plugin,destination)
    step([str(UNITY),'-batchmode','-nographics','-quit','-projectPath',str(project),'-executeMethod','BuildSteamFixture.Build','-logFile',str(editorlog)],600)
    assert 'STEAM_FIXTURE_BUILD=Succeeded' in editorlog.read_text(errors='replace')
    exe=player/'Fixture.exe';assert exe.is_file()
    assert sha(plugin)==sha(player/'Fixture_Data/Plugins/x86_64/steam_fixture.dll')
    result=subprocess.run([str(exe),'-batchmode','-nographics','-logFile',str(log)],timeout=180,env=env)
    text=log.read_text(errors='replace');cases=re.findall(r'^STEAM_TEST ([a-z0-9_]+)=(PASS|FAIL)$',text,re.M)
    print('PLAYER_EXIT',result.returncode,'CASES',cases,flush=True)
    assert result.returncode==0 and Counter(n for n,_ in cases)==Counter(EXPECTED) and all(s=='PASS' for _,s in cases) and 'STEAM_TEST RESULT failures=0' in text
    print('PASS plugin_sha256',sha(plugin),'exe_sha256',sha(exe),'gameassembly_sha256',sha(player/'GameAssembly.dll'),'metadata_sha256',sha(player/'Fixture_Data/il2cpp_data/Metadata/global-metadata.dat'),flush=True)
    print('Evidence retained at',base,'(owned project/player/logs).',flush=True)
if __name__=='__main__':run()
