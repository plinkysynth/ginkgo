#!/usr/bin/env python3
import re, sys, xml.etree.ElementTree as ET
from math import hypot, pi

def parse_css_styles(root):
    css=''.join(''.join(el.itertext()) for el in root.findall('.//{http://www.w3.org/2000/svg}style'))
    styles={}
    for sel, body in re.findall(r'([.#][^{\s]+)\s*\{([^}]*)\}', css):
        kv={}
        for part in body.split(';'):
            if ':' in part:
                k,v=part.split(':',1); kv[k.strip()]=v.strip()
        if sel.startswith('.'): styles[sel[1:]]=kv
    return styles

def parse_color(s):
    if not s or s == 'none': return None
    s = s.strip()
    if s.startswith('#'):
        if len(s)==4: r,g,b=[int(c*2,16) for c in s[1:]]
        else: r=int(s[1:3],16); g=int(s[3:5],16); b=int(s[5:7],16)
        return (r<<16)|(g<<8)|b
    m = re.match(r'rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', s)
    if m: r,g,b = map(int, m.groups()); return (r<<16)|(g<<8)|b
    named={'black':0x000000,'white':0xFFFFFF,'gray':0x808080,'grey':0x808080,'red':0xFF0000,'green':0x00FF00,'blue':0x0000FF}
    return named.get(s.lower())

def get_inline_style(el, key):
    v = el.attrib.get(key)
    if v: return v
    st = el.attrib.get('style')
    if not st: return None
    for part in st.split(';'):
        if not part: continue
        k,_,val = part.partition(':')
        if k.strip()==key: return val.strip()
    return None

def tok_path(d):
    for m in re.finditer(r'[AaCcHhLlMmQqSsTtVvZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d):
        t=m.group(0)
        if re.fullmatch(r'[A-Za-z]', t): yield t
        else: yield float(t)

def reflect(px,py,cx,cy): return 2*cx-px, 2*cy-py
def lerp(a,b,t): return a + (b-a)*t
def lerp2(p,q,t): return (lerp(p[0],q[0],t), lerp(p[1],q[1],t))

def path_to_cubic_segments(d):
    out=[]; it=iter(tok_path(d)); x=y=sx=sy=0.0; last_cmd=None; last_c2=None
    def add_seg(p0,p1,p2,p3): out.append((p0,p1,p2,p3))
    while True:
        try: t = next(it)
        except StopIteration: break
        if isinstance(t,str): cmd=t
        else:
            if last_cmd is None: raise ValueError('Path data starts with number')
            cmd=last_cmd; it = iter([t, *list(it)])
        abs_cmd = cmd.upper(); rel = cmd.islower()
        if abs_cmd=='M':
            x = x + next(it) if rel else next(it); y = y + next(it) if rel else next(it); sx,sy=x,y; last_cmd='M'; last_c2=None
            while True:
                try: n=next(it)
                except StopIteration: break
                if isinstance(n,str): it = iter([n,*list(it)]); break
                nx = x + n if rel else n; ny = y + next(it) if rel else next(it)
                add_seg((x,y), lerp2((x,y),(nx,ny),1/3), lerp2((x,y),(nx,ny),2/3), (nx,ny))
                x,y=nx,ny; last_cmd='L'; last_c2=None
        elif abs_cmd=='C':
            while True:
                try:
                    x1 = (x + next(it)) if rel else next(it); y1 = (y + next(it)) if rel else next(it)
                    x2 = (x + next(it)) if rel else next(it); y2 = (y + next(it)) if rel else next(it)
                    x3 = (x + next(it)) if rel else next(it); y3 = (y + next(it)) if rel else next(it)
                except StopIteration: break
                add_seg((x,y),(x1,y1),(x2,y2),(x3,y3)); x,y=x3,y3; last_c2=(x2,y2); last_cmd=cmd
                try:
                    n=next(it)
                    if isinstance(n,str): it=iter([n,*list(it)]); break
                    else: it=iter([n,*list(it)])
                except StopIteration: break
        elif abs_cmd=='S':
            while True:
                try:
                    x2 = (x + next(it)) if rel else next(it); y2 = (y + next(it)) if rel else next(it)
                    x3 = (x + next(it)) if rel else next(it); y3 = (y + next(it)) if rel else next(it)
                except StopIteration: break
                if last_cmd and last_cmd.upper() in ('C','S') and last_c2:
                    x1,y1 = reflect(*last_c2,x,y)
                else:
                    x1,y1 = x,y
                add_seg((x,y),(x1,y1),(x2,y2),(x3,y3)); x,y=x3,y3; last_c2=(x2,y2); last_cmd=cmd
                try:
                    n=next(it)
                    if isinstance(n,str): it=iter([n,*list(it)]); break
                    else: it=iter([n,*list(it)])
                except StopIteration: break
        elif abs_cmd=='L':
            while True:
                try:
                    nx = (x + next(it)) if rel else next(it); ny = (y + next(it)) if rel else next(it)
                except StopIteration: break
                add_seg((x,y), lerp2((x,y),(nx,ny),1/3), lerp2((x,y),(nx,ny),2/3), (nx,ny))
                x,y=nx,ny; last_cmd=cmd; last_c2=None
                try:
                    n=next(it)
                    if isinstance(n,str): it=iter([n,*list(it)]); break
                    else: it=iter([n,*list(it)])
                except StopIteration: break
        elif abs_cmd=='H':
            while True:
                try: nx = (x + next(it)) if rel else next(it)
                except StopIteration: break
                ny=y
                add_seg((x,y), lerp2((x,y),(nx,ny),1/3), lerp2((x,y),(nx,ny),2/3), (nx,ny))
                x=nx; last_cmd=cmd; last_c2=None
                try:
                    n=next(it)
                    if isinstance(n,str): it=iter([n,*list(it)]); break
                    else: it=iter([n,*list(it)])
                except StopIteration: break
        elif abs_cmd=='V':
            while True:
                try: ny = (y + next(it)) if rel else next(it)
                except StopIteration: break
                nx=x
                add_seg((x,y), lerp2((x,y),(nx,ny),1/3), lerp2((x,y),(nx,ny),2/3), (nx,ny))
                y=ny; last_cmd=cmd; last_c2=None
                try:
                    n=next(it)
                    if isinstance(n,str): it=iter([n,*list(it)]); break
                    else: it=iter([n,*list(it)])
                except StopIteration: break
        elif abs_cmd=='Z':
            nx,ny=sx,sy
            add_seg((x,y), lerp2((x,y),(nx,ny),1/3), lerp2((x,y),(nx,ny),2/3), (nx,ny))
            x,y=nx,ny; last_cmd=cmd; last_c2=None
        else:
            last_cmd=cmd; last_c2=None
    return out

def extract(svg_path):
    tree=ET.parse(svg_path); root=tree.getroot()
    css = parse_css_styles(root)
    segs=[]
    def stroke_info(el):
        color=None; width=None
        classes = (el.attrib.get('class') or '').split()
        for cls in classes:
            st = css.get(cls, {})
            color = color or st.get('stroke')
            width = width or st.get('stroke-width')
        color = parse_color(get_inline_style(el,'stroke') or color)
        width = float(get_inline_style(el,'stroke-width') or width or 1.0)
        return color,width
    for el in root.iter():
        tag = el.tag.split('}')[-1]
        if tag=='rect': continue
        if tag=='path':
            d=el.attrib.get('d'); 
            color,width=stroke_info(el)
            if not d or color is None: continue
            for p0,p1,p2,p3 in path_to_cubic_segments(d):
                segs.append((p0,p1,p2,p3,color,width))
        elif tag=='circle':
            cx=float(el.attrib.get('cx','0')); cy=float(el.attrib.get('cy','0')); r=float(el.attrib.get('r','0'))
            color,width=stroke_info(el)
            if color is None or r<=0: continue
            segs.append(((cx,cy),(cx+r,cy),(0.0,0.0),(0.0,0.0),color,width))
        elif tag=='ellipse':
            # If rx≈ry treat as circle
            cx=float(el.attrib.get('cx','0')); cy=float(el.attrib.get('cy','0'))
            rx=float(el.attrib.get('rx','0')); ry=float(el.attrib.get('ry','0'))
            color,width=stroke_info(el)
            if color is None or rx<=0 or ry<=0: continue
            if abs(rx-ry) < 1e-6:
                r=rx
                segs.append(((cx,cy),(cx+r,cy),(0.0,0.0),(0.0,0.0),color,width))
            # else: ignore non-circular ellipse for now
    return segs

def seg_length(p0,p1,p2,p3,color,width):
    if p2==(0.0,0.0) and p3==(0.0,0.0) and p1!=p0:
        r = hypot(p1[0]-p0[0], p1[1]-p0[1])
        return 2.0*pi*r
    chord = hypot(p3[0]-p0[0], p3[1]-p0[1])
    ctrl  = hypot(p1[0]-p0[0], p1[1]-p0[1]) + hypot(p2[0]-p1[0], p2[1]-p1[1]) + hypot(p3[0]-p2[0], p3[1]-p2[1])
    return 0.5*(ctrl + chord)


def dist(p0,p1):
    return hypot(p0[0]-p1[0], p0[1]-p1[1])

def close(p0,p1,eps=5):
    return dist(p0,p1) < eps

def reorder_segments(segs, eps=1e-3):
    out=[]
    penpos=(0.0,0.0)
    stroke_count = 0
    while len(segs) > 0:
        bestseg = None
        bestdist = 1e10
        for seg in segs:
            p0 = seg[0]
            d = dist(p0, penpos)
            if d >= bestdist: continue
            is_start = True
            for seg2 in segs:
                if seg2 != seg and (close(p0, seg2[0]) or close(p0, seg2[3])):
                    is_start = False
                    break
            if is_start:
                bestdist = d
                bestseg = seg
        if bestseg is None: bestseg=segs[0]
        print(f'bestseg: {bestseg}')
        segs.remove(bestseg)
        out.append((*bestseg, stroke_count))
        penpos = bestseg[0]
        while len(segs) > 0:
            bestseg=None
            for seg in segs:
                if close(seg[0], penpos):
                    bestseg=seg
                    segs.remove(seg)
                    break
                if close(seg[3], penpos):
                    bestseg=(seg[3], seg[2], seg[1], seg[0], seg[4], seg[5])
                    segs.remove(seg)
                    break
            if bestseg is None: break
            penpos = bestseg[3]
            out.append((*bestseg, stroke_count))
        stroke_count += 1
    print(f'stroke_count: {stroke_count}')
    return out

def fmtf(x):
    s = f'{x:.6g}'
    if ('e' not in s) and ('.' not in s): s += '.0'
    return s + 'f'


def emit_c(segs, var='segments'):
    # compute bbox
    minx=miny=1e30; maxx=maxy=-1e30
    def acc(p): 
        nonlocal minx,miny,maxx,maxy
        x,y=p; minx=min(minx,x); miny=min(miny,y); maxx=max(maxx,x); maxy=max(maxy,y)
    for p0,p1,p2,p3,color,width,stroke_count in segs:
        if p2==(0.0,0.0) and p3==(0.0,0.0) and p1!=p0:
            r = ((p1[0]-p0[0])**2 + (p1[1]-p0[1])**2) ** 0.5
            acc((p0[0]-r,p0[1]-r)); acc((p0[0]+r,p0[1]+r))
        else:
            acc(p0); acc(p1); acc(p2); acc(p3)
    lines=[
        'typedef struct { float2 p0,p1,p2,p3; unsigned int color; float stroke_width; float length; int stroke_count; } segment_t;',
        f'segment_t {var}[] = {{'
    ]
    for p0,p1,p2,p3,color,width,stroke_count in segs:
        L = seg_length(p0,p1,p2,p3,color,width)
        fmt=lambda p: f'{{{fmtf(p[0])},{fmtf(p[1])}}}'
        lines.append(f'  {{ {fmt(p0)}, {fmt(p1)}, {fmt(p2)}, {fmt(p3)}, 0x{color:06X}u, {fmtf(width)}, {fmtf(L)}, {stroke_count} }},')
    lines.append('};')
    lines.append(f'const float2 segments_bbox_min = {{{fmtf(minx)},{fmtf(miny)}}};')
    lines.append(f'const float2 segments_bbox_max = {{{fmtf(maxx)},{fmtf(maxy)}}};')
    return '\n'.join(lines)

def main():
    if len(sys.argv)<3:
        print('usage: svg2segments.py input.svg output.h', file=sys.stderr); sys.exit(2)
    segs=extract(sys.argv[1])
    #segs.sort(key=lambda seg: seg[0][0])
    segs=reorder_segments(segs)
    with open(sys.argv[2],'w') as f: f.write(emit_c(segs))
    print(f'// {len(segs)} segments written to {sys.argv[2]}')
if __name__=='__main__': main()
