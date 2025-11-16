// effectively a list of all the nodes and infix operators we support in the min language
// for now that's enough :)


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
NODE(N_RAND)
NODE(N_RAND2)
NODE(N_RANDI) // takes a max value
NODE(N_CC) 
NODE(N_LAMBDA)
NODE(N_CAT)
NODE(N_FASTCAT)
NODE(N_GRID)
NODE(N_PARALLEL)
NODE(N_RANDOM)
NODE(N_POLY)
// op_type, srcname, numparams, num_optional_params, precedence
OP(N_OP_EUCLID, "(", 3, 1, 400)
OP(N_OP_REPLICATE, "!", 1, 0, 300)
OP(N_OP_ELONGATE, "@", 1, 0, 300)
OP(N_OP_DEGRADE, "?", 0, 1, 200)
OP(N_OP_TIMES, "*", 1, 0, 200)
OP(N_OP_DIVIDE, "/", 1, 0, 200)

OP(N_OP_ROUND, "round", 0, 0, 100)
OP(N_OP_FLOOR, "floor", 0, 0, 100)
OP(N_OP_RANGE, "range", 2, 0, 100)
OP(N_OP_RANGE2, "range2", 2, 0, 100)
OP(N_OP_RIBBON, "rib", 1, 1, 100)
OP(N_OP_BLEND, "blend", 2, 0, 100)

#define X(x, str, ...) OP(N_##x, str, 1, 0, 50)
#include "params.h"

OP(N_OP_NEVER, "never", 1, 0, 75)
OP(N_OP_RARELY, "rarely", 1, 0, 75)
OP(N_OP_SOMETIMES, "sometimes", 1, 0, 75)
OP(N_OP_OFTEN, "often", 1, 0, 75)
OP(N_OP_ALWAYS, "always", 1, 0, 75)

OP(N_OP_ADSR, "adsr", 4, 0, 50)
OP(N_OP_ADSR2, "adsr2", 4, 0, 50)

OP(N_OP_LATE, "late", 1, 0, 10)
OP(N_OP_EARLY, "early", 1, 0, 10)
OP(N_OP_CLIP, "clip", 1, 0, 10)
OP(N_OP_ADD, "add", 1, 0, 10)
OP(N_OP_SUB, "sub", 1, 0, 10)
OP(N_OP_MUL, "mul", 1, 0, 10)
OP(N_OP_DIV, "div", 1, 0, 10)
OP(N_OP_FIT, "fit", 0, 0, 10)
OP(N_OP_FITN, "fitn", 1, 0, 10)


#undef OP
#undef NODE