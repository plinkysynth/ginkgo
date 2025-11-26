## very rough docs for ginkgo

ginkgo loads 4 'tabs', selected with `F1`-`F4`.

| key | description | 
| --- | ---- | 
| F1 | VISUALS -> a glsl file | 
| F2 | MUSIC -> a cpp file | 
| F3 | sample selector (no file associated) | 
| F4 | CANVAS -> a .canvas file | 

you can specify filenames on the commandline and it will load them into the appropriate tab.
pressing the current tab's function key will toggle if the code is visible.

### keyboard shortcuts

when you see ⌘ on windows, use CTRL

| key | description | 
| --- | ---- | 
| F1 | select VISUALS tab
| F2 |  select MUSIC tab
| F3 |  select CANVAS tab
| F4 |  select SAMPLE SELECTOR tab
| F12 |  tap tempo; tap once to re-sync, or more than once to set a tempo. overwrites /bpm pattern in the music tab.
| ⌘Enter | re-evaluate patterns in the first two tabs.
| ⌘P |  play/pause
| ⌘, ⌘. |  stop; press again to rewind.
| ⌘[ ⌘] |  ffwd / rewind
| ⌘S |  save current tab only
| ⌘Q |  quit (warning: doesnt save)
| ⌘up / ⌘down | when the cursor is over a number, it will nudge it up or down a bit
| ⌘- / ⌘= | change text size

in CANVAS tab, `[ ]` changes brush size; `{ }` changes brush softness; `1-9` selects colour.

patterns can be updated without saving by pressing ⌘Enter.
this also recompiles the GLSL code in the visuals tab (F1).
the C code in the audio tab (F2) is recompiled only when you press save (⌘S), as it is a slower/more glitch prone process.
so I tend to get into the habit of modifying uzu patterns a lot, and C code less.

### patterns in code

in both VISUALS and MUSIC tab you can write
```
#ifdef PATTERNS
...
#endif
```
any number of times and the part between is written in pattern language

each pattern begins at the beginning of a line with a `/name/like/this`. 

patterns can stretch over multiple lines.

you can 'call' patterns from other patterns just by referring to their name beginning with a `/`.

### glsl code
in the visuals tab (F1), the code not inside the patterns ifdef is just glsl. think of it like shadertoy.

### c code

`ginkgo.h` provides a small collection of types and functions that can be used to build up any DSP or synth voices.
```
struct song : public song_base_t {
  synth_t s1;
  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.75);
      x+=reverb(x*0.5f);
      return x;
  }
};
```

in your cpp file, you should declare a struct song, as above, derived from song_base_t.
in this you place any objects that are meant to carry state (eg delay lines or filters or reverbs or synth voices).

`do_sample` is called once for each sample, at 96khz, and simply returns a stereo sample to be played. the type `stereo` is just a pair of floats, `l` and `r`. 
the type `synth_t` takes the name of a pattern, and renders it a sample at a time into audio. the second parameter is the level.



### uzu dialect

#### basics
words are separated by spaces rather than dots - this isnt javascript like tidal.

at the moment the language is rather simplistic and not many words are imeplemented. start with basic `< > [ ] { } | , * /` which behave roughly as in strudel.

operator precedence may be funky, in which case wrap things in  `[ ]` or `< >` to clarify.

alternatively break up complex patterns into `/named` parts and call the parts as needed.

notes with sharps or flats are written with s and b, like `cs3 db4` or `Cs3 Db4` 

#### ranges
notes and numbers can have 'ranges', such as `0.3-11` or `c4-g7`
in that case, a random new value will be chosen on each cycle.

#### scales
scales and chords can be written like `cmaj` although at the moment I dont support the various inversions.
scales are mostly used to 'quantize' existing note patterns, rather than to specify voicing etc.

#### sliders
you can embed a silder in the code by writing
`/*=======*/0.5` inline. the length of the slider is set by the number of === and the 0.5 is the current value.

you can also set the range of the slider by setting a number at the start and end: `/*0====10*/5` will give a slider over the range of 0 to 10.

note that like any other code change, it will only take effect when the code is next saved or re-evaluated (⌘Enter or ⌘S).

however you can also attach an in-code slider to a midi cc value from 0-7, like `/*=====*/cc(3)`. the 8 cc sliders are also always visible at the bottom right of the screen. note that they actually map to midi cc 16 to 23, corresponding to the default CCs used by the MusicThingModular mu8 controller. more flexibility of the cc mapping todo.
the advantage of an in-code cc slider is that it updates in realtime, not just on next-evaluate.

####  curves
docs todo. write curves inside single quotes. this lets you easily set up shapes for automation.

#### tracker in patterns
docs todo. write tracker sections over multiple lines between `#` symbol.
the idea is to write uzu expressions over time, with each line being 1/16th of a cycle.

#### synth parameters
in strudel, I find the distinction around `n()` vs `note()` a little confusing and ginkgo may work slightly differently.

the way it works in ginkgo is that each 'event' has a number of parameters controlling the synth (on the C/dsp side).

the uzu language can set any of these parameters. note that `^` is roughly equivalent to midi velocity or in modular terms, 'gate level'.

here is a list of parameters at the time of writing:

`: note s ^ cut res env2vcf env2fold env2dist dist fold att dec sus rel att2 dec2 sus2 rel2 gain pan loops loope from to vib vibf trem tremf glide string`

the C/dsp code can use these parameters as it wishes. but the default synth at the moment is polyphonic, with vibrato, tremolo, two ADSRs 
(the first for a VCA, the second for VCF though it can be routed to distortion and fold), a soft clipper for distortion and a wave folder.
so in ginkgo you 'set' these by writing the name of the parameter followed by a pattern or number, and you can chain.
so `[c a f e] fold 0.3 dist 0.9 att <0.1 0.2> dec <0.3 0.5>` and so on.

the `:` operator is a bit special - it lets you apply either note or sample or 'number' (eg sub-sample) or scale.
so you can write:

`sd : c4` - plays a snare drum an octave up (`c3` is default)

`c4 : sd` - the same

`c4 : sd : 9` - the same, but the 9th sample

`sd:11` - the 11th snaredrum

`[c d e f g a b]:cmaj` - play a sequence of notes, but snap to c major triad.

### list of uzu words

TODO: go thru `node_types.h` and `params.h` and document them all.




