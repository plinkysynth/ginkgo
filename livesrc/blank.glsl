/*
hello and welcome to ginkgo!

this is blank.glsl, an empty GLSL shader to make visuals.
feel free to edit the code below - in the style of shadertoy - to create an image
press Cmd+S to save & recompile, or Cmd+Enter to just recompile.

Other useful shortcut keys:

F1 = VISUALS (glsl + uzu)
F2 = AUDIO (C++ + uzu)
F3 = CANVAS (drawing!)
F4 = Sample selector
F12 = tap tempo. (updates a /bpm pattern in the audio tab)

If you press F1-F4 repeatedly it toggles the visibility of the code on that tab.
On F1 and F2 tabs, the part inside #ifdef PATTERNS is written in a tidal-like uzu lang.

Cmd+P = play/pause
Cmd+Comma / Cmd+Period = stop
Cmd+[] = skip forward or backward
Cmd+Q = quit

For more information, see docs/docs.md
*/
#ifdef PATTERNS
/vispat 0 1
#endif
// bloom 0
// tex assets/photo.jpg

uniform vec4 vispat;
vec4 pixel(vec2 uv) {
	vec4 o=vec4(0.);
	for (int iter=0;iter<4;++iter) {
    	vec2 p0 = vec2(sin(iTime*vec2(0.010,0.0120)+vec2(21.,62.)+uv.y*0.01));
        vec2 p1 = vec2(sin(iTime*vec2(0.015,0.0112)+vec2(31.,12.)+uv.y*0.01));
        vec2 p = mix(p0,p1,uv.x);
        p=sin(p)*1.5+0.5+0.175-vec2(1.4);
            
        o+=texture(uTex, p);
        uv.x+=1.1;
        uv+=uv*mat2(.8,.6,-.6,.8)*0.9;
    }
    return o;
    return vec4(0.01,0.02,0.03,1.);
}

