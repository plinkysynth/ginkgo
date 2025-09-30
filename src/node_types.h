// effectively a list of all the nodes and infix operators we support in the mini language
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
NODE(N_CAT)
NODE(N_FASTCAT)
NODE(N_PARALLEL)
NODE(N_RANDOM)
NODE(N_POLY)
// op_type, srcname, numparams, num_optional_params
OP(N_OP_IDX, ":", 1, 0)
OP(N_OP_TIMES, "*", 1, 0)
OP(N_OP_DIVIDE, "/", 1, 0)
OP(N_OP_DEGRADE, "?", 1, 1)
OP(N_OP_REPLICATE, "|", 1, 0)
OP(N_OP_ELONGATE, "@", 1, 0)
OP(N_OP_EUCLID, "(", 3, 1)
OP(N_OP_PLY, "ply", 1, 0)

#undef OP
#undef NODE