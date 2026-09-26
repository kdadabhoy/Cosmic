"""VM01 log scan: count shader build failures in logs, separating the VM02 fixture's
deliberate failures (every one names ux_v0_) from any other (which must be zero).
Usage: py -3 scan_shader_errors.py <label> <file> [<file> ...]"""
import re, sys

label, files = sys.argv[1], sys.argv[2:]
total = {'compile': 0, 'link': 0, 'create_err': 0, 'create_err_other': 0}
for f in files:
    lines = open(f, encoding='utf-8', errors='replace').read().splitlines()
    for i, line in enumerate(lines):
        if 'Shader compilation failure' in line:
            total['compile'] += 1
        if 'Shader link failure' in line:
            total['link'] += 1
        if '[error] Shader::Create:' in line:
            total['create_err'] += 1
            if 'ux_v0_' not in line:
                total['create_err_other'] += 1
                print('  NON-FIXTURE:', f, i + 1, line[:220])
ok = total['create_err_other'] == 0 and total['link'] == 0 and total['compile'] <= total['create_err']
print('%s: files=%d compile_failures=%d link_failures=%d Shader::Create_errors=%d non_fixture=%d -> %s'
      % (label, len(files), total['compile'], total['link'], total['create_err'], total['create_err_other'],
         'CLEAN' if ok else 'NOT CLEAN'))
sys.exit(0 if ok else 1)
