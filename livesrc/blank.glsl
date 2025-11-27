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
#endif

// tex assets/font_brutalita.png


vec4 pixel(vec2 uv) {
	return texture(uTex, uv);
    return vec4(0.01,0.02,0.03,1.);
}

#ifdef C 
void update_frame(void) {
    //printf("hello from update frame\n");
}
#endif
