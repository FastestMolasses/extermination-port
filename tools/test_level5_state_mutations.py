#!/usr/bin/env python3
"""Deliberate mistakes must be rejected by the original-entry dispatcher oracle."""
import os
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'src/game/em_level5_state.c'
MUTATIONS=(
    ('zero-key', 'case 0: key=1;', 'case 0: key=0;', 'key/0/0'),
    ('record-stride', 'offset+=0xA060;', 'offset+=0xA050;', 'count/768/3'),
    ('signed-count', '(int32_t)i<(int32_t)word(&s,0x275C2C)', 'i<word(&s,0x275C2C)', 'count/768/4294967295'),
    ('read-order', 'key=area+byte(&s,0x810701);\n    zero(&s,0x275C18);',
                   'zero(&s,0x275C18);\n    key=area+byte(&s,0x810701);', 'key/4/0'),
    ('skip-effects', 'if (h->w_001F0310(h->ctx)<0) return fail(&s,0x001F0310,2);',
                     'if (0) return fail(&s,0x001F0310,2);', 'chain/changed/'),
)
def main():
    parent=ROOT/'build/level-oracle-audit/state-mutations'; parent.mkdir(parents=True,exist_ok=True)
    run=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
    original=SOURCE.read_text()
    for name,before,after,subset in MUTATIONS:
        assert original.count(before)==1,(name,'mutation anchor')
        copy=run/name; copy.mkdir()
        (copy/SOURCE.name).write_text(original.replace(before,after))
        (copy/'em_level5_state.h').write_text((SOURCE.parent/'em_level5_state.h').read_text())
        env=dict(os.environ,EM_LEVEL5_STATE_SOURCE=str(copy),EM_LEVEL5_STATE_ONLY=subset,EM_TEST_JOBS='1')
        with (copy/'oracle.log').open('wb') as log:
            result=subprocess.run(['python3','tools/test_level5_state_reference.py'],cwd=ROOT,env=env,
                                  stdout=log,stderr=subprocess.STDOUT)
        text=(copy/'oracle.log').read_text()
        assert result.returncode!=0 and 'FAIL ' in text,(name,'not rejected by an assertion')
        print(name,'rejected')
    print(f'PASS {len(MUTATIONS)} deliberate mistakes rejected; private source copies:',run)
if __name__=='__main__':main()
