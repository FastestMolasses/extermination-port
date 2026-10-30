#!/usr/bin/env python3
"""Export a private EMMV cache using an installed FFmpeg command-line tool.
No FFmpeg code or library is linked into the port. Input: the .mov generated
locally by tools/export_movie.py. Every picture is kept; PCM is stream-copied.
This decoder's video pixels are NOT claimed identical to Apple's decoder.
"""
import argparse, json, math, os, struct, subprocess
from pathlib import Path
from fractions import Fraction

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('movie',type=Path)
    ap.add_argument('--ffmpeg',default='ffmpeg')
    ap.add_argument('--ffprobe',default='ffprobe')
    args=ap.parse_args(); source=args.movie.resolve(); out=Path(str(source)+'.emmv')
    if out.exists(): ap.error('cache exists; choose a fresh locally exported movie path')
    probe=json.loads(subprocess.check_output([args.ffprobe,'-v','error','-show_streams','-of','json',str(source)]))
    video=[s for s in probe['streams'] if s['codec_type']=='video']
    audio=[s for s in probe['streams'] if s['codec_type']=='audio']
    if len(video)!=1 or len(audio)!=1: raise SystemExit('expected one video and one PCM track')
    v,a=video[0],audio[0]
    if a['codec_name']!='pcm_s16le' or int(a['sample_rate'])!=48000 or a['channels']!=2:
        raise SystemExit('expected original stereo PCM16/48000 track')
    w,h=int(v['width']),int(v['height'])
    if not 0<w<=8192 or not 0<h<=8192: raise SystemExit('invalid picture dimensions')
    # Integer timestamp + rational time base, avoiding ffprobe's rounded
    # presentation_time strings. This is the same binary64 division as macOS.
    frames=json.loads(subprocess.check_output([args.ffprobe,'-v','error','-select_streams','v:0',
        '-show_frames','-show_entries','frame=pts,width,height','-of','json',str(source)]))['frames']
    timebase=Fraction(v['time_base']); pts=[]
    for f in frames:
        t=float(int(f['pts'])*timebase)
        if f['width']!=w or f['height']!=h or not math.isfinite(t) or (pts and t<=pts[-1]):
            raise SystemExit('changing dimensions or non-increasing timestamps')
        pts.append(t)
    if not pts: raise SystemExit('no pictures')
    pcm_frames=Fraction(int(a['duration_ts']))*Fraction(a['time_base'])*48000
    if pcm_frames.denominator!=1: raise SystemExit('partial PCM frame')
    pcm_frames=int(pcm_frames)
    temp=out.with_suffix(out.suffix+'.partial')
    if temp.exists(): ap.error('partial cache exists; inspect it before retrying')
    base=[args.ffmpeg,'-v','error','-nostdin','-i',str(source)]
    # fps_mode passthrough preserves display order with no duplicated/dropped
    # pictures. The game, not the decoder, schedules them against its fields.
    with temp.open('xb') as dst:
        dst.write(struct.pack('<4s5IQ',b'EMMV',1,len(pts),w,h,0,pcm_frames))
        proc=subprocess.Popen(base+['-map','0:v:0','-fps_mode','passthrough','-pix_fmt','rgba','-f','rawvideo','pipe:1'],stdout=subprocess.PIPE)
        try:
            for t in pts:
                image=proc.stdout.read(w*h*4)
                if len(image)!=w*h*4: raise RuntimeError('missing decoded picture')
                dst.write(struct.pack('<d',t)); dst.write(image)
            if proc.stdout.read(1) or proc.wait()!=0: raise RuntimeError('unexpected extra pictures or decoder failure')
        finally:
            proc.stdout.close()
            if proc.poll() is None: proc.terminate(); proc.wait()
        # Streamcopy, no decoder/resampler: original little-endian sample bytes.
        proc=subprocess.Popen(base+['-map','0:a:0','-c:a','copy','-f','s16le','pipe:1'],stdout=subprocess.PIPE)
        count=0
        try:
            for chunk in iter(lambda:proc.stdout.read(1024*1024),b''):
                count+=len(chunk); dst.write(chunk)
            if proc.wait()!=0 or count!=pcm_frames*4: raise RuntimeError('PCM frame count differs')
        finally:
            proc.stdout.close()
            if proc.poll() is None: proc.terminate(); proc.wait()
    os.rename(temp,out)
    print(f'cache exported: {len(pts)} pictures, {pcm_frames} stereo PCM frames (local only)')
if __name__=='__main__': main()
