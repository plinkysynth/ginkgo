// effectively a list of all the nodes and infix operators we support in the min language
// for now that's enough :)

/* TODO more functions we might want

n -> set the idx 
note -> set the note
number/value/v -> set the value


almostNever, and similar
ply
rib
seg
clip
add,sub,mul (and note versions? nadd, nsub, nmul?)
late / early
fast slow and all the others that are equivalent to the mininotation ones
*/


#ifndef NODE
#define NODE(x, ...) x,
#endif
#ifndef OP
#define OP NODE
#endif
NODE(N_LEAF)
NODE(N_CALL)
NODE(N_CURVE)
NODE(N_SIN)
NODE(N_COS)
NODE(N_SIN2)
NODE(N_COS2)
NODE(N_SAW)
NODE(N_SAW2)
NODE(N_RAND)
NODE(N_RAND2)
NODE(N_RANDI) // takes a max value
NODE(N_CAT)
NODE(N_FASTCAT)
NODE(N_GRID)
NODE(N_PARALLEL)
NODE(N_RANDOM)
NODE(N_POLY)
// op_type, srcname, numparams, num_optional_params
OP(N_OP_TIMES, "*", 1, 0)
OP(N_OP_DIVIDE, "/", 1, 0)
OP(N_OP_IDX, ":", 1, 0)
OP(N_OP_DEGRADE, "?", 0, 1)
OP(N_OP_REPLICATE, "!", 1, 0)
OP(N_OP_ELONGATE, "@", 1, 0)
OP(N_OP_EUCLID, "(", 3, 1)

OP(N_OP_LATE, "late", 1, 0)
OP(N_OP_EARLY, "early", 1, 0)
OP(N_OP_CLIP, "clip", 1, 0)
OP(N_OP_ADD, "add", 1, 0)
OP(N_OP_SUB, "sub", 1, 0)
OP(N_OP_MUL, "mul", 1, 0)
OP(N_OP_DIV, "div", 1, 0)
OP(N_OP_RANGE, "range", 2, 0)
OP(N_OP_RANGE2, "range2", 2, 0)
OP(N_OP_PLY, "ply", 1, 0)

//OP(N_OP_STRUCT, "struct", 3, 1)
//OP(N_OP_PLY, "ply", 1, 0)
OP(N_OP_NOTE, "note", 1, 0)
OP(N_OP_S, "s", 1, 0)
OP(N_OP_GATE, "^", 1, 0)
OP(N_OP_ATTACK, "att", 1, 0)
OP(N_OP_DECAY, "dec", 1, 0)
OP(N_OP_SUSTAIN, "sus", 1, 0)
OP(N_OP_RELEASE, "rel", 1, 0)
OP(N_OP_ADSR, "adsr", 4, 0)
OP(N_OP_GAIN, "gain", 1, 0)
OP(N_OP_PAN, "pan", 1, 0)


#undef OP
#undef NODE