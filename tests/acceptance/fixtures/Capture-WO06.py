"""Repository-local WO-06 evidence hashes and readable raw-log excerpts."""
from pathlib import Path
import hashlib, json, re, subprocess, sys
repo=Path(__file__).resolve().parents[3]
ev=repo/'docs/plans/2d-stability-2026-09-16/evidence/WO-06'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def git(*args):return subprocess.check_output(['git','-c','core.safecrlf=false',*args],cwd=repo)
sources=set()
for directory in ['Cosmic/src/telemetry','Projects/SF_Telem/src']:
    sources.update(p for p in (repo/directory).iterdir() if p.suffix in ('.h','.cpp'))
sources.update(repo/p for p in ['CMakeLists.txt','Cosmic/CMakeLists.txt','tests/CMakeLists.txt',
 'Cosmic/src/utils/DataExport.cpp','Cosmic/src/utils/DataExport.h','Cosmic/src/utils/AtomicOutput.cpp',
 'Cosmic/src/utils/AtomicOutput.h','tests/test_wo06.cpp','tests/test_wo06_host.cpp','tests/WO06HostFixture.cpp',
 'tests/FakeSerialTransport.h','tests/WO05HostFixture.cpp','tests/WO05HostReport.h','tests/test_wo05_matrix.cpp',
 'tests/acceptance/Run-Acceptance.ps1','tests/acceptance/AcceptanceRunner.psm1','tests/acceptance/fixtures/Run-WO05Host.ps1'])
sources.update((repo/'tests/acceptance/fixtures').glob('*WO06*'))
sources.update((repo/'tests/acceptance/manifests').glob('wo06*.json'))
source_hashes={str(p.relative_to(repo)).replace('\\','/'):sha(p) for p in sorted(sources) if p.is_file()}
binary_hashes={}
for config in ['Debug','Release']:
    for name in ['Cosmic.dll','CosmicTests.exe','SF_Telem.dll','WO05HostFixture.dll','WO06HostFixture.dll']:
        p=repo/'build/Runtime'/config/name
        binary_hashes[str(p.relative_to(repo)).replace('\\','/')]=sha(p)
fixtures=repo/'tests/acceptance/fixtures/wo06'
manifest=json.loads((fixtures/'manifest.json').read_text(encoding='utf-8'))
for name,want in manifest['sha256'].items():assert sha(fixtures/name)==want
raw_logs=sorted(set(ev.glob('*.log'))|set(ev.glob('*/*.log')))
log_hashes={str(p.relative_to(ev)).replace('\\','/'):sha(p) for p in raw_logs}
excerpt=[]
for p in raw_logs:
    lines=p.read_text(encoding='utf-8',errors='replace').splitlines()
    if p.name.startswith('build'):
        chosen=lines[-8:]
    else:
        chosen=[line for line in lines if any(word in line for word in
          ['WO06 ','WO05 matrix:','WO05 cancel owner','OpenGL ','test cases:','assertions:','Status:',
           'ERROR:','FATAL ERROR:','Traceback','AssertionError','failed load','Flush FAILED'])]
        chosen=chosen[:24]+(['[... excerpt omitted; full raw log hash retained ...]'] if len(chosen)>48 else [])+chosen[24:] if len(chosen)<=48 else chosen[:24]+['[... full raw log retained locally ...]']+chosen[-24:]
    if chosen:excerpt+=['\n## '+str(p.relative_to(ev)),*chosen]
(ev/'raw-log-excerpts.txt').write_text('\n'.join(excerpt)+'\n',encoding='utf-8')
cache=(repo/'build/CMakeCache.txt').read_text(encoding='utf-8')
effective=[line for line in cache.splitlines() if re.match(r'(COSMIC_2D_ONLY|COSMIC_BUILD_TESTS|CMAKE_COMMAND|CMAKE_GENERATOR|CMAKE_CXX_COMPILER|CMAKE_SYSTEM_VERSION)',line)]
assert 'COSMIC_2D_ONLY:BOOL=ON' in effective
identity={'tested_source_parent':git('rev-parse','HEAD').decode().strip(),
 'origin_main_local_ref':git('rev-parse','origin/main').decode().strip(),
 'sf_stable_reference':git('rev-parse','origin/SF-Stable').decode().strip(),
 'raw_git_diff_sha256':hashlib.sha256(git('diff','HEAD')).hexdigest(),
 'python_version':sys.version,'effective_cache':effective,
 'protected_untracked_plan_sha256':sha(repo/'Cosmic - 2D Trunk Consolidation & Acceptance Plan.md'),
 'source_sha256':source_hashes,'binary_sha256':binary_hashes,
 'immutable_fixture_sha256':manifest['sha256'],'raw_log_sha256':log_hashes}
(ev/'final-hashes.json').write_text(json.dumps(identity,indent=2)+'\n',encoding='utf-8')
print('WO06 evidence hashes:',len(source_hashes),'sources;',len(binary_hashes),'binaries;',len(log_hashes),'raw logs')
