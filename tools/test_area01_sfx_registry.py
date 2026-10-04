#!/usr/bin/env python3
"""AREA01 registry/source-bank proof; original instructions, no audio device.

The function census records entries, not sound arguments. Its linear
constant-call audit is reported separately from dispatcher/driver equality.
All disc-derived output stays in ignored build/.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import time

import export_area01_common as A
import export_area01_sfx as B
import export_sfx_registry as X
import reference_mode as mode
import test_area11_sfx_reference as T

OUT=A.ROOT/'build/level2/sfx-registry'


def caller_audit(ram,entries):
    scan=X.load_decomp_module('sfx_request_probe')
    delta=json.loads((A.DECOMP/'build/s87/census/a01_delta.json').read_text())
    funcs={int(r['addr'],16):(r['name'],r['size']) for r in delta['functions']
           if 'area01' in r['phases']}
    calls={k:(r,m,0) for k,(r,m) in scan.SOUND_ENTRIES.items()}
    while True:
        sites=[];grew=False
        for start,(name,size) in funcs.items():
            for pc in range(start,start+size,4):
                word=scan._word(ram,pc)
                if word>>26 not in (2,3) or scan._target(pc,word) not in calls:continue
                target=scan._target(pc,word);reg,mask,add=calls[target]
                kind,*value=scan._trace(ram,start,pc,reg)
                row=dict(function=hex(start),site=hex(pc),callee=hex(target),kind=kind)
                if kind=='const':row['ids']=[hex((v+add)&mask) for v in sorted(value[0])]
                elif kind=='arg' and start not in calls:
                    calls[start]=(value[0],mask,value[1]+add);grew=True
                sites.append(row)
        if not grew:break
    ids=sorted({int(i,16) for r in sites for i in r.get('ids',[])})
    known={e['id'] for e in entries if e['scope'] in ([-1,-1],[1,0])}
    missing=sorted(set(ids)-known)
    assert not missing,('unexported constant in AREA01-censused caller',missing)
    return dict(functions=len(funcs),sites=sites,constant_ids=ids,missing=missing,
                limit='Function hits do not prove execution of each conditional sound site; '
                      'computed IDs, script data, and unrecorded branches are not a route request census.')


def key(e):return tuple(e['scope']),e['id']
def sample_key(s):return s['container'],s['offset'],s['adpcm_sha256']


def recorded_owners(caps):
    flames={}
    for cap in caps:
        if not cap.name.startswith('a01_'):continue
        kinds=collections.Counter(cap.ram[a+0xD] for a in range(0x7A5640,0x7D4640,0x2F0)
            if cap.ram[a] and A.u32(cap.ram,a+0x10)==0x1E3D90)
        flames[cap.name]=dict(kinds)
    rows=0;bridge=collections.Counter()
    for path in sorted(A.ROUTE_A01.glob('a01_*/trace.json')):
        for row in json.loads(path.read_text())['rows']:
            if row.get('area')!='0100':continue
            rows+=1
            for name in ('r41_8261A0','r42_8261A0'):
                image=bytes.fromhex(row[name]['h'])
                bridge[f'{image[4]}/{image[5]}']+=1
    return dict(flame_endpoint_variants=flames,area01_sampled_rows=rows,
                bridge_lifecycle_phase=dict(bridge),
                limit='Endpoint actor kinds and sampled branch state are not per-call audio argument traces.')


def main():
    start=time.monotonic();p=argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--registry',type=Path,default=A.ROOT/'assets/sfx/sfx_registry.emsr')
    args=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    report=json.loads(args.registry.with_suffix('.json').read_text())
    raw=args.registry.read_bytes();assert hashlib.sha256(raw).hexdigest()==report['registry_sha256']
    entries,samples,ladder=T.parse_emsr(raw)
    # Regenerate the exact pre-extension list with the same original exporter.
    saved=X.AREA01_IDS,X.AREA01_SHARED_IDS
    X.AREA01_IDS=X.AREA01_SHARED_IDS=()
    try:old=X.export(OUT/'baseline')
    finally:X.AREA01_IDS,X.AREA01_SHARED_IDS=saved
    old_entries,old_samples,old_ladder=T.parse_emsr((OUT/'baseline/sfx_registry.emsr').read_bytes())
    indexed={key(e):e for e in entries};json_index={key(e):e for e in report['entries']}
    assert all(indexed[key(e)]==e for e in old_entries)
    assert all(json_index[key(e)]==e for e in old['entries'])
    assert samples[:len(old_samples)]==old_samples and ladder==old_ladder
    assert report['samples'][:len(old['samples'])]==old['samples']
    # The existing full AREA01 bank export is an independent installed
    # catalog. Normalize only registry-local sample indices for comparison.
    area=json.loads((A.ROOT/'assets/area01/sfx/sfx_registry.json').read_text())
    def normalized(entry,catalog):
        entry=json.loads(json.dumps(entry))
        for event in entry.get('events',[]):
            if 'sample' in event:event['sample']=sample_key(catalog['samples'][event['sample']])
        return entry
    for e in area['entries']:
        assert normalized(e,area)==normalized(json_index[key(e)],report),hex(e['id'])
    caps=A.captures();binding=B.area_binding(caps);groups=binding[0][(1,0)]['groups']
    # Each sample stays inside its original body region; independent ADPCM
    # decoding checks first-pass PCM, while the established catalog pins
    # repeat-body PCM and source identity for all AREA01 samples.
    banks=[bank for group in groups.values() for bank in group]
    area_raw=T.parse_emsr((A.ROOT/'assets/area01/sfx/sfx_registry.emsr').read_bytes())[1]
    area_samples={sample_key(s):area_raw[s['index']] for s in area['samples']}
    source_bytes=loops=0
    for meta,decoded in zip(report['samples'],samples):
        matching=[b for b in banks if b.name==meta['container'] and
                  b.body<=meta['offset'] and meta['offset']+meta['adpcm_bytes']<=b.body+b.body_size]
        if not matching:continue       # preserved AREA11 / office sample
        bank=matching[0];adpcm=bank.data[meta['offset']:meta['offset']+meta['adpcm_bytes']]
        assert hashlib.sha256(adpcm).hexdigest()==meta['adpcm_sha256']
        assert X.A.decode_adpcm(adpcm)==decoded['pcm'][:2*decoded['frames']]
        flags=adpcm[1::16];assert flags[-1]&1 and not any(f&1 for f in flags[:-1])
        loop=max((i for i,f in enumerate(flags) if f&4),default=-1)*28 if flags[-1]&2 else None
        assert loop==decoded['loop_start']==meta['loop_start']
        if sample_key(meta) in area_samples:assert decoded==area_samples[sample_key(meta)]
        source_bytes+=len(adpcm);loops+=loop is not None
    audit=caller_audit(caps[0].ram,entries)
    audit['recorded_owners']=recorded_owners(caps)
    (OUT/'caller-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    lib,words=T.native_driver();elf=T.Elf();dispatch=0;unsupported=collections.Counter()
    unbound_results=collections.Counter();unbound_ids=collections.defaultdict(set)
    native=lib.shim_new(str(args.registry).encode(),T.STREAM_VOICES,0,0,0,1);assert native
    area_entries=[e for e in report['entries'] if e['scope']==[1,0]]
    global_extra=json_index[(-1,-1),0x1AC]
    for e in area_entries+[global_extra]:
        assert lib.shim_entry_state(native,e['id'],*e['scope'])==e['state']
    # Distinct regional records must coexist; changing scope must not
    # replace AREA11's entries or use AREA11 PCM to satisfy AREA01.
    for sid in (0x411,0x412,0x413):
        assert indexed[((1,0),sid)]['ops']!=indexed[((11,0),sid)]['ops']
    lib.shim_free(native)
    # Pin the finite loader cap with otherwise valid registries. Added
    # synthetic ABSENT entries carry no wave or original data.
    at=24+len(ladder)*2+sum(16+len(e['ops'])*32 for e in entries)
    for count in (2048,2049):
        blob=bytearray(raw[:at])
        struct.pack_into('<I',blob,12,count)
        for i in range(count-len(entries)):
            blob+=struct.pack('<IhhBBHHH',0x1000+i,0,0,2,0,0,0,0)
        blob+=raw[at:];path=OUT/f'cap-{count}.emsr';path.write_bytes(blob)
        check=lib.shim_new(str(path).encode(),T.STREAM_VOICES,0,0,0,1)
        assert bool(check)==(count==2048),count
        if check:lib.shim_free(check)
    for label,blob in (('truncated',raw[:-1]),('extra',raw+b'\0')):
        path=OUT/f'malformed-{label}.emsr';path.write_bytes(blob)
        check=lib.shim_new(str(path).encode(),T.STREAM_VOICES,0,0,0,1)
        assert not check,label
    run_caps=caps if mode.FULL else caps[:1]
    dispatch_entries=mode.select(area_entries+[global_extra],64,0x1FB9F0,
        axes=(lambda e:(e['state'],e.get('reason'),e.get('bank')),),
        keep=lambda i,e:e['id'] in (0x411,0x412,0x413,0x44E,0x8A9,0x1AC) or e.get('reason')=='unbound bank')
    for cap in run_caps:
        registry=T.Registry(elf,cap.ram,report,args.registry,(1,0),groups)
        for e in dispatch_entries:
            o=registry.oracle();o.calls[0x1157F0]=lambda _:None;o.calls[0x1191F0]=lambda _:None
            if 'record' in e:
                group,bank=e['record'][:2];address=B.D_00281D50+4*(group*0x14+bank)
                o.write(address,cap.ram[address:address+4])
            o.run(0x1FB9F0,(e['id'],0x1000,0x1000,0x1000))
            refused=T.signed(o.r[2])<0
            if e.get('reason')=='unbound bank':
                # The exporter deliberately refuses an unknown bank. This
                # is not a claim that original hardware emits a voice.
                unbound_results['refused' if refused else 'track']+=1
                unbound_ids[hex(e['id'])].add('refused' if refused else 'track')
            else:assert refused==(e['state']==X.STATE_ABSENT),(cap.name,hex(e['id']),e['state'])
            dispatch+=1
    for e in area_entries:
        if e['state']==X.STATE_UNSUPPORTED:unsupported[e['reason']]+=1
    registry=T.Registry(elf,caps[0].ram,report,args.registry,(1,0),groups)
    T._SFX.update(X=X,lib=lib,registry=registry,sample_address=registry.address,native_words=words)
    audible=[e for e in area_entries+[global_extra] if e['state']==X.STATE_AUDIBLE]
    if not mode.FULL:audible=[e for e in audible if e['id'] in (0x411,0x412,0x413,0x44E,0x8A9,0x1AC)]
    voices=sum(T._registry_case((e,0x1000,0x1000)) for e in audible)
    summary=dict(status='PASS',mode=mode.MODE,entries=len(entries),samples=len(samples),
                 preserved_entries=len(old_entries),preserved_samples=len(old_samples),
                 bank_captures=len(caps),native_lookup_cases=len(area_entries)+1,
                 cap_and_malformed_cases=4,
                 original_dispatch_cases=dispatch,sequencer_cases=len(audible),key_ons=voices,
                 source_adpcm_bytes=source_bytes,source_loop_samples=loops,
                 area_states=dict(collections.Counter(e['state'] for e in area_entries)),
                 unsupported_reasons=dict(unsupported),constant_ids=len(audit['constant_ids']),
                 original_unbound_results=dict(unbound_results),
                 original_unbound_ids={k:sorted(v) for k,v in unbound_ids.items()},
                 constant_call_sites=sum(r['kind']=='const' for r in audit['sites']),
                 computed_call_sites=sum(r['kind']=='computed' for r in audit['sites']),
                 seconds=round(time.monotonic()-start,2))
    (OUT/f'report-{mode.MODE}.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('AREA01 SFX registry:',json.dumps(summary,sort_keys=True))


if __name__=='__main__':main()
