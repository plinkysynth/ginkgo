live coding innit

[link to initial video on bluesky](https://bsky.app/profile/mmalex.bsky.social/post/3lzopol4hbc2c)

** NOT INTENDED TO BE USABLE FOR ANYONE EXCEPT ME **
code is public just in the spirit of sharing, not releasing OSS.
I have not decided on a license yet, so for now 'all rights reserved'.

copyright alex 'mmalex' evans 2025 

much inspiration from https://garten.salat.dev/ and from the london live coding event i attended in august 2025.

this is basically two tabs: 
* F1=GLSL shader code for visuals, 
* F2=C code for audio.

F1 is basically shadertoy 'native'; F2 compiles a small shared object using clang (mac only for now) that it relinks and runs a function for every sample.

I'm still fleshing it out but some ideas that are started:

- state is somewhat preserved between reloads
- theres a 'bump allocator' for floats that various things need (like lfos, delay lines, etc), so you can just declare oscillators, lfos, etc, and use them without declaring state explicitly.
- im trying to keep it as simple & explicit as possible, while keeping it terse

    - terseness is a *must*, and not usually a strong point for C, so Im experimenting with the tradeoffs (footguns & implicit state management magic vs terseness)
- for midi, it keeps track of cc's (currently on my musicthingmodular mu8) and maps cc 16-47 to 'variables' S0 thru S15.
    - when you mention a slider, it allocates a tiny bit of state that the editor then shows on the side of the screen to allow mouse editing.
but also, if you say S0 (cc16) in multiple places in the code, it maps the closest one to the cursor to the midi! 
so in theory we can overload the same physical knob depending on where the code cursor is. spicy....

the editor attempts to parse glsl and clang errors and displays them inline.

im trying out table based minblep for the sawtooth osc for the first time. 

the text editor is a bit of a mess. i started with stb_textedit but didnt like some things so started again from scratch, and as a result it is still in-flux.

im trying out running at silly Fs (96khz) and some-what high quality downsampling. having worked at 32khz in plinky land for a while, this feels odd.

i intend to stay 'everthing happens a sample at a time in one shader-like function' as long as I can, including for rhythmic stuff (ie no distinction between dsp rate stuff and note rate stuff). i dont know how powerful laptops are any more, so this may bite me. or be awesome. who knows.

I wonder how long these decisions will last.

I wonder if I'll get to making any nice music with it. hope so.
