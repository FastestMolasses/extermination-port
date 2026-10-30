#!/usr/bin/env python3
"""Exercise the portable cache reader; compare full API output to native macOS.
Metadata only in the report. All pictures and audio stay local.
"""
import argparse, ctypes as C, hashlib, json, os, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class Info(C.Structure): _fields_=[('pictures',C.c_uint32),('audio_frames',C.c_uint64)]
class Frame(C.Structure):
    _fields_=[('pixels',C.c_void_p),('width',C.c_uint),('height',C.c_uint),('stride',C.c_uint),('pts_seconds',C.c_double),('picture_index',C.c_uint64)]
def load(path):
    lib=C.CDLL(str(path)); lib.em_movie_open.argtypes=[C.c_char_p]; lib.em_movie_open.restype=C.c_void_p
    lib.em_movie_close.argtypes=[C.c_void_p]
    lib.em_movie_info.argtypes=[C.c_void_p,C.POINTER(Info)]
    lib.em_movie_next_picture.argtypes=[C.c_void_p,C.POINTER(Frame)]
    lib.em_movie_read_audio.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.c_long];lib.em_movie_read_audio.restype=C.c_long
    return lib

def digest(lib,path):
    m=lib.em_movie_open(os.fsencode(path)); assert m, 'allocation failure'
    try:
        info=Info(); assert lib.em_movie_info(m,C.byref(info))==0, 'cannot open movie/cache'
        video=hashlib.sha256();pts=hashlib.sha256();pcm=hashlib.sha256();frame=Frame()
        for n in range(info.pictures):
            assert lib.em_movie_next_picture(m,C.byref(frame))==1 and frame.picture_index==n
            pts.update(struct.pack('<d',frame.pts_seconds))
            for y in range(frame.height): video.update(C.string_at(frame.pixels+y*frame.stride,frame.width*4))
        assert lib.em_movie_next_picture(m,C.byref(frame))==0
        out=(C.c_int16*(4093*2))();count=0
        while True:
            n=lib.em_movie_read_audio(m,out,4093); assert n>=0
            if not n: break
            count+=n;pcm.update(bytes(out)[:n*4])
        assert count==info.audio_frames
        return dict(pictures=info.pictures,audio_frames=count,video_sha256=video.hexdigest(),pts_sha256=pts.hexdigest(),pcm_sha256=pcm.hexdigest())
    finally: lib.em_movie_close(m)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('movie',type=Path)
    ap.add_argument('--out',required=True,type=Path);ap.add_argument('--native',action='store_true');args=ap.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    suffix='.dylib' if sys.platform=='darwin' else '.so'
    common=[os.environ.get('CC','cc'),'-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-fPIC','-dynamiclib' if sys.platform=='darwin' else '-shared','-I'+str(ROOT/'src')]
    cache=args.out.resolve()/('cache'+suffix)
    subprocess.run(common+[str(ROOT/'src/platform/linux/em_movie_cache.c'),'-o',str(cache)],check=True)
    result={'cache':digest(load(cache),args.movie.resolve())}
    if args.native:
        assert sys.platform=='darwin','native comparison needs macOS AVFoundation'
        native=args.out.resolve()/('native'+suffix)
        subprocess.run(common+[str(ROOT/'src/platform/mac/em_movie_mac.m'),'-framework','Foundation','-framework','AVFoundation','-framework','CoreMedia','-framework','CoreVideo','-o',str(native)],check=True)
        result['native']=digest(load(native),args.movie.resolve())
        assert result['cache']==result['native'],'cache API differs from native decoder'
    (args.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('movie cache API: PASS'+(' (every picture, timestamp and PCM sample equals AVFoundation)' if args.native else ''))
if __name__=='__main__': main()
