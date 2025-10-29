// code for turning patterns (a tree of nodes) into haps (a flat list of events with times and values)

template <typename T> void arrsetlencap(T *&arr, int len, int cap) {
    assert(len <= cap);
    stbds_arrsetcap(arr, cap);
    stbds_arrsetlen(arr, len);
}

// convert the parsed node structure into an SoA bfs tree. also 'squeezes' single child lists.
pattern_t pattern_maker_t::make_pattern(const char *key, int index_to_add_to_start_end) {
    pattern_t p = {.key = key, .curvedata = curvedata};
    int n_input = stbds_arrlen(nodes);
    if (err || root < 0 || root >= n_input)
        return p;
    int_pair_t q[n_input];
    int qhead = 1;
    q[0] = {root, -1};
    for (int i = 0; i < qhead; i++) {
        Node *n = nodes + q[i].k;
        bool has_one_child = n->first_child >= 0 && nodes[n->first_child].next_sib < 0;
        bool has_zero_children = n->first_child < 0;

        if ((has_one_child || has_zero_children) &&
            (n->type == N_CAT || (n->type == N_FASTCAT && n->total_length == 1.f) || n->type == N_PARALLEL)) {
            if (has_zero_children) {
                // skip this node! its empty.

                continue;
            }
            q[i--].k = n->first_child; // replace this node with its only child
            continue;
        }
        int bfs_parent = q[i].v;
        int my_bfs_idx = stbds_arrlen(p.bfs_nodes);
        // float previous_sibling_length = 0.f;
        if (bfs_parent >= 0 && p.bfs_nodes[bfs_parent].first_child < 0) {
            p.bfs_nodes[bfs_parent].first_child = my_bfs_idx;
        }
        if (bfs_parent >= 0) {
            p.bfs_nodes[bfs_parent].num_children++;
        }

        token_info_t start_end = {n->start + index_to_add_to_start_end, n->end + index_to_add_to_start_end, 0.f};
        float_minmax_t min_max_value = {n->min_value, n->max_value};
        bfs_node_t node = {n->type, n->value_type, 0, -1};
        // float cumulative_length = n->total_length + previous_sibling_length;
        if (!p.bfs_nodes) {
            arrsetlencap(p.bfs_start_end, 0, n_input);
            arrsetlencap(p.bfs_min_max_value, 0, n_input);
            arrsetlencap(p.bfs_nodes, 0, n_input);
            arrsetlencap(p.bfs_kids_total_length, 0, n_input);
            arrsetlencap(p.bfs_grid_time_offset, 0, n_input);
        }
        stbds_arrpush(p.bfs_start_end, start_end);
        stbds_arrpush(p.bfs_min_max_value, min_max_value);
        stbds_arrpush(p.bfs_nodes, node);
        stbds_arrpush(p.bfs_kids_total_length, n->total_length);
        stbds_arrpush(p.bfs_grid_time_offset, n->linenumber);
        for (int child = n->first_child; child >= 0; child = nodes[child].next_sib) {
            assert(child < n_input);
            q[qhead++] = {child, my_bfs_idx};
        }
    }
    if (!stbds_arrlen(p.bfs_nodes)) {
        p.unalloc();
    }
    return p;
}

void _pretty_print_nodes(const char *src, const char *srcend, pattern_t *p, int i = 0, int depth = 0, int numsiblings = 1) {
    if (i < 0 || p->bfs_nodes == NULL)
        return;
    int c0 = p->bfs_start_end[i].start;
    int c1 = p->bfs_start_end[i].end;
    if (srcend > src && srcend[-1] == '\n')
        --srcend;
    c0 = clamp(c0, 0, (int)(srcend - src));
    c1 = clamp(c1, 0, (int)(srcend - src));
    printf(COLOR_GREY "%d " COLOR_BLUE "%.*s" COLOR_BRIGHT_YELLOW "%.*s" COLOR_BLUE "%.*s" COLOR_RESET, i, c0, src, c1 - c0,
           src + c0, (int)(srcend - src - c1), src + c1);
    for (int j = 0; j < depth + 1; j++) {
        printf("  ");
    }
    bfs_node_t *n = p->bfs_nodes + i;
    switch (n->value_type) {
    case VT_SOUND: {
        Sound *sound = get_sound_by_index((int)p->bfs_min_max_value[i].mx);
        printf("%d - %s - kidslen %g val %g-%g %s\n", i, node_type_names[n->type], p->bfs_kids_total_length[i],
               p->bfs_min_max_value[i].mn, p->bfs_min_max_value[i].mx, sound ? sound->name : "");
        break;
    }
    case VT_NOTE: {
        printf("%d - %s - kidslen %g val %g-%g %s-%s\n", i, node_type_names[n->type], p->bfs_kids_total_length[i],
               p->bfs_min_max_value[i].mn, p->bfs_min_max_value[i].mx, print_midinote((int)p->bfs_min_max_value[i].mn),
               print_midinote((int)p->bfs_min_max_value[i].mx));
        break;
    }
    case VT_SCALE: {
        printf("%d - %s - kidslen %g val scale %03x-%03x\n", i, node_type_names[n->type], p->bfs_kids_total_length[i],
               (int)p->bfs_min_max_value[i].mn, (int)p->bfs_min_max_value[i].mx);
        break;
    }
    default:
        printf("%d - %s - maxlen %g val %g-%g\n", i, node_type_names[n->type], p->bfs_kids_total_length[i],
               p->bfs_min_max_value[i].mn, p->bfs_min_max_value[i].mx);
        break;
    }
    if (n->first_child >= 0) {
        _pretty_print_nodes(src, srcend, p, n->first_child, depth + 1, n->num_children);
    }
    if (numsiblings > 1) {
        _pretty_print_nodes(src, srcend, p, i + 1, depth, numsiblings - 1);
    }
}

void pretty_print_nodes(const char *src, const char *srcend, pattern_t *p, int i = 0, int depth = 0, int numsiblings = 1) {
    int n = srcend - src;
    char *copy_src = temp_cstring_from_span(src, srcend);
    srcend = copy_src + n;
    src = copy_src;
    for (int i = 0; i < n; i++)
        if (copy_src[i] == '\n')
            copy_src[i] = '.';
    _pretty_print_nodes(src, srcend, p, i, depth, numsiblings);
}
/*
TODO the compact-hap mode is: 6*4=24 bytes per hap. and then a bulky one off to the side.
hap_time t0, t1;
int node; // index of the node that generated this hap.
uint32_t valid_params; // which params have been assigned for this hap.
int hapid;
float first_valid_value;


(fast)cat:
    loop over whole children  that overlap the query, and emit them. REMEMBER: QUERY IS ONLY AN OPTIMIZATION
    *:
        eval right side; for each right hap, eval left side, CLIP TO RIGHT HAP
    : or assignment in genereal: 'structure from left'
        eval left side; eval right side; for each left hap, find the right hap that contains its start point, and set the value
    euclid:
        eval numsteps in query range
        find the earliest time that overlaps the query range; we can use the fact that they are subdivided to take the last
'subdivided' one. eval setsteps & rot & 'left hand side' over this expanded range, and sort by t1 for each step-hap, subdivided
suitably for each setstep hap that overlaps... for each rot hap that overlaps... if rhythm says yes and it overlaps query range, for
each left hand side hap that overlaps... emit a new hap with timing from this rhythm, but value from the overlap structure: eval
structure in range filter structure that is value '0' or rest or whatever eval the 'left' in the range of the remaining structure
haps for each structure-hap, for each 'left' hap that overlaps... emit a new hap with timing from this structure, but value from the
overlap
*/

float pattern_t::get_length(int nodeidx) {
    if (nodeidx < 0)
        return 0.f;
    int t = bfs_nodes[nodeidx].type;
    if (t == N_OP_REPLICATE || t == N_OP_ELONGATE || t == N_GRID)
        return bfs_min_max_value[nodeidx].mx; // use max value as length
    return 1.f;
}

bool pattern_t::_append_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid) {
    if (dst.s >= dst.e || t0 > t1 || nodeidx < 0)
        return false;
    bfs_node_t *n = bfs_nodes + nodeidx;
    int value_type = n->value_type;
    if (value_type <= VT_NONE)
        return false;
    float_minmax_t minmax = bfs_min_max_value[nodeidx];
    float v;
    if (minmax.mn != minmax.mx && value_type != VT_SCALE) { // randomize in range
        float t = (pcg_mix(pcg_next(hapid)) & 0xffffff) * (1.f / 0xffffff);
        v = lerp(minmax.mn, minmax.mx, t);
        if ((n->value_type == VT_NOTE || n->value_type == VT_SCALE) &&
            minmax.mx - minmax.mn >= 1.f) // if it's a range of at least two semitone,
                                          // quantize to nearest semitone
            v = roundf(v);
    } else
        v = minmax.mx; // take the max value (for scales, this is the note)
    if (value_type == VT_SOUND && v < 1.f)
        return false; // its a rest! is_rest

    hap_t *hap = dst.s++;
    hap->t0 = t0;
    hap->t1 = t1;
    hap->node = nodeidx;
    hap->hapid = hapid;
    hap->valid_params = 1 << value_type;
    hap->params[value_type] = v;
    hap->scale_bits = (value_type == VT_SCALE) ? minmax.mn : 0;
    return true;
}

// filter out haps that are outside the query range a-b as an optimization,
// but ALSO filter out haps whose start is outside the from-to hap range.
// this actually culls haps determinstically! eg [a b c] * [2 1] will have a rest in the second half.
void pattern_t::_filter_haps(hap_span_t left_haps, hap_time speed_scale, hap_time tofs, hap_time a, hap_time b, hap_time from,
                             hap_time to) {
    for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
        left_hap->t0 = (left_hap->t0 + tofs) / speed_scale;
        left_hap->t1 = (left_hap->t1 + tofs) / speed_scale;
        // if we include this next line, it clips eg <c a f e>/2 to only have a hap in the first half of each note.
        // so we should let haps overhang the right edge of the queried range, I guess is what Im saying.
        // if (left_hap->t1 > to)
        //     left_hap->t1 = to;
        if (left_hap->t0 > b || left_hap->t1 < a || left_hap->t0 + hap_eps < from || left_hap->t0 - hap_eps > to) {
            left_hap->valid_params = 0;
        }
    }
}

int pattern_t::_apply_values(hap_span_t &dst, int tmp_size, float viz_time, hap_t *structure_hap, int value_node_idx,
                             filter_cb_t filter_cb, value_cb_t value_cb, size_t context) {
    hap_time t0 = structure_hap->t0;
    hap_t tmp_mem[tmp_size];
    hap_span_t tmp = {tmp_mem, tmp_mem + tmp_size};
    hap_span_t value_haps = _make_haps(tmp, tmp_size, viz_time, value_node_idx, t0, t0 + hap_eps,
                                       hash2_pcg(structure_hap->hapid, value_node_idx + (int)(t0 * 1000.f)), true);
    int count = 0;
    int structure_hapid = structure_hap->hapid;
    bool do_an_empty_one = value_haps.empty();
    for (hap_t *right_hap = value_haps.s; right_hap < value_haps.e || do_an_empty_one; right_hap++) {
        do_an_empty_one = false;
        if (right_hap && !right_hap->valid_params)
            continue;
        int new_hapid = hash2_pcg(structure_hapid, right_hap ? right_hap->hapid : 0);
        int num_copies = filter_cb ? filter_cb(structure_hap, right_hap, new_hapid) : 1;
        for (int i = 0; i < num_copies; i++) {
            hap_t *target = structure_hap;
            if (count++) {
                if (dst.s >= dst.e)
                    break;
                target = dst.s++;
                *target = *structure_hap;
                target->hapid = new_hapid;
            }
            if (value_cb)
                value_cb(target, right_hap, context + i);
        }
    }
    if (count == 0)
        structure_hap->valid_params = 0; // no copies wanted!
    return count;
}

void pattern_t::_apply_unary_op(hap_span_t &dst, int tmp_size, float viz_time, int parent_idx, hap_time t0, hap_time t1,
                                int parent_hapid, bool merge_repeated_leaves, filter_cb_t filter_cb, value_cb_t value_cb,
                                size_t context) {
    bfs_node_t *n = bfs_nodes + parent_idx;
    if (n->num_children < 1)
        return;
    int right_child = (n->num_children > 1) ? n->first_child + 1 : -1;
    hap_span_t left_haps =
        _make_haps(dst, tmp_size, viz_time, n->first_child, t0, t1, hash2_pcg(parent_hapid, n->first_child), merge_repeated_leaves);
    for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
        _apply_values(dst, tmp_size, viz_time, left_hap, right_child, filter_cb, value_cb, context);
    }
}

static inline int quantize_note_to_scale(int scalebits, int root, int note) {
    scalebits = scalebits & 0xfff;
    if (!scalebits)
        return note;
    note -= root;
    int octave = note / 12;
    if (note < 0)
        octave--;
    int degree = note - octave * 12;
    scalebits |= scalebits << 12;
    int degreemask = (1 << degree) - 1;
    // find the next bit up that is set.
    // 001001100100 <- scalebits (repeated 2 octaves)
    // 00000000n000
    // 000000000111 <- degreemask - clear these bits
    int above = __builtin_ctz(scalebits & ~degreemask);
    // find the next bit down that is set
    // 001001100100 <- scalebits (repeated 2 octaves)
    // 0000n0000000 <- note up an octave
    // 000011111111 <- degreemask - keep these bits
    degreemask = ((2 << 12) << degree) - 1;
    int below = 19 - __builtin_clz(scalebits & degreemask); // 19... because 32-12-1. can be negative
    note = root + octave * 12 + ((above - degree < degree - below) ? above : below);
    return note;
}

static inline int scale_index_to_note(int scalebits, int root, int index) {
    if (!scalebits)
        return root;
    int num_notes_in_scale = __builtin_popcount(scalebits);
    int octave = index / num_notes_in_scale;
    int note = root + octave * 12;
    index %= num_notes_in_scale;
    while (index)
        scalebits &= (scalebits - 1), index--;
    note += __builtin_ctz(scalebits);
    return note;
}

void apply_value_func(hap_t *target, hap_t *right_hap, size_t param_idx) {
    if (!right_hap || param_idx >= P_LAST)
        return;
    int src_idx = param_idx;
    if (!right_hap->has_param(src_idx)) {
        if (!right_hap->has_param(P_NUMBER))
            return;
        src_idx = P_NUMBER;
    }
    target->params[param_idx] = right_hap->params[src_idx];
    target->valid_params |= 1 << param_idx;
}

void add_value_func(hap_t *target, hap_t *right_hap, size_t negative) { // param_idx 1=sub
    if (!right_hap || !right_hap->has_param(P_NUMBER) || !target->valid_params)
        return;
    int param_idx = __builtin_ctz(target->valid_params);
    float v = right_hap->params[P_NUMBER];
    target->params[param_idx] += negative ? -v : v;
}

hap_span_t pattern_t::_make_haps(hap_span_t &dst, int tmp_size, float viz_time, int nodeidx, hap_time a, hap_time b, int hapid,
                                 bool merge_repeated_leaves) {
    if (nodeidx < 0 || a > b || !bfs_nodes)
        return {};
    hap_span_t rv = {dst.s, dst.s};
    if (dst.s >= dst.e)
        return rv;
    bfs_node_t *n = bfs_nodes + nodeidx;
    hap_time speed_scale = 1.f;
    hap_time kids_total_length = bfs_kids_total_length[nodeidx];
    int param = P_NUMBER;
    switch (n->type) {
    case N_CALL: {
        int patidx = (int)bfs_min_max_value[nodeidx].mx;
        if (patidx >= 0 && patidx < stbds_shlen(G->patterns_map)) {
            pattern_t *pat = &G->patterns_map[patidx];
            pat->_make_haps(dst, tmp_size, viz_time, 0, a, b, hapid, merge_repeated_leaves);
        }
        break;
    }
    case N_LEAF: {
        bool appended = false;
        if (merge_repeated_leaves)
            appended = _append_hap(dst, nodeidx, floor(a + hap_eps), ceil(b - hap_eps), hapid);
        else
            for (int i = floor(a + hap_eps); i < b; ++i) {
                appended |= _append_hap(dst, nodeidx, i, i + 1, hash2_pcg(hapid, i));
            }
        if (appended && viz_time >= 0.f) {
            bfs_start_end[nodeidx].last_evaled_glfw_time = viz_time;
        }
        break;
    }
    case N_POLY:
        speed_scale = (bfs_min_max_value[nodeidx].mx > 0) ? bfs_min_max_value[nodeidx].mn
                      : (n->first_child >= 0)             ? bfs_kids_total_length[n->first_child]
                                                          : 1.;
    case N_PARALLEL: {
        if (speed_scale <= 0.f)
            break;
        for (int childidx = 0; childidx < n->num_children; ++childidx) {
            int child = n->first_child + childidx;
            _make_haps(dst, tmp_size, viz_time, child, a * speed_scale, b * speed_scale, hash2_pcg(hapid, childidx),
                       merge_repeated_leaves);
        }
        _filter_haps({rv.s, dst.s}, speed_scale, 0., a, b, floor(a + hap_eps), ceil(b - hap_eps));
        break;
    }
    case N_OP_LATE:
    case N_OP_EARLY:
    case N_OP_ELONGATE:
    case N_OP_REPLICATE:
    case N_OP_TIMES:
    case N_OP_DIVIDE: {
        if (n->first_child < 0)
            break;
        if (n->num_children == 1) {
            if (n->type == N_OP_ELONGATE && bfs_min_max_value[nodeidx].mx != 0.f) {
                hap_time speed_scale = 1.0 / bfs_min_max_value[nodeidx].mx;
                hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, a * speed_scale, b * speed_scale,
                                                  hash2_pcg(hapid, n->first_child), merge_repeated_leaves);
                _filter_haps(left_haps, speed_scale, 0., a, b, -1e9, 1e9);
            }
            break;
        }
        hap_t tmp_mem[tmp_size];
        hap_span_t tmp = {tmp_mem, tmp_mem + tmp_size};
        hap_span_t right_haps =
            _make_haps(tmp, tmp_size, viz_time, n->first_child + 1, a, b, hash2_pcg(hapid, n->first_child + 1), true);
        for (hap_t *right_hap = right_haps.s; right_hap < right_haps.e; right_hap++) {
            float num = right_hap->get_param(P_NUMBER, 0.f);
            hap_time speed_scale = 1.;
            hap_time tofs = 0.;
            switch (n->type) {
            case N_OP_LATE:
                tofs = num;
                break;
            case N_OP_EARLY:
                tofs = -num;
                break;
            case N_OP_TIMES:
                speed_scale = num;
                break;
            case N_OP_ELONGATE:
            case N_OP_REPLICATE:
            case N_OP_DIVIDE:
                speed_scale = num ? 1. / num : 0.;
                break;
            }
            if (speed_scale <= 0.f)
                continue;
            hap_time from = (n->type == N_OP_ELONGATE) ? -1e9 : right_hap->t0;
            hap_time to = (n->type == N_OP_ELONGATE) ? 1e9 : right_hap->t1;
            hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, max(from, a) * speed_scale,
                                              min(to, b) * speed_scale, hash2_pcg(hapid, right_hap->hapid), merge_repeated_leaves);
            _filter_haps(left_haps, speed_scale, tofs, a, b, from, to);
        }
        break;
    }
    case N_RANDOM: {
        if (n->num_children <= 0)
            break;
        for (int i = floor(a + hap_eps); i < b; ++i) {
            int childidx = (hash2_pcg(hapid, i)) % n->num_children;
            _make_haps(dst, tmp_size, viz_time, n->first_child + childidx, i, i + 1, hash2_pcg(hapid, i), merge_repeated_leaves);
        }
        break;
    }
    case N_OP_IDX: { // take structure from the left; value(s) from the right. copy as needed
        if (n->num_children < 2)
            break;
        // the : operator is quite flexible for applying scales to notes or numbers.
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, a, b, nodeidx, merge_repeated_leaves, nullptr,
            [](hap_t *target, hap_t *right_hap, size_t param_idx) {
                if (!right_hap || !right_hap->valid_params)
                    return;
                param_idx = __builtin_ctz(right_hap->valid_params);
                if (param_idx == P_NUMBER && target->has_param(P_SCALEBITS) && !target->has_param(P_NOTE)) {
                    // apply a number to a scale -> index scale into a note
                    target->params[P_NOTE] =
                        scale_index_to_note(target->scale_bits, target->params[P_SCALEBITS], (int)right_hap->params[P_NUMBER]);
                    target->valid_params |= 1 << P_NOTE;
                } else if (param_idx == P_SCALEBITS && target->has_param(P_NUMBER) && !target->has_param(P_NOTE)) {
                    // apply a scale to a number -> index scale into a note
                    target->params[P_NOTE] =
                        scale_index_to_note(right_hap->scale_bits, right_hap->params[P_SCALEBITS], (int)target->params[P_NUMBER]);
                    target->valid_params |= (1 << P_NOTE) | (1 << P_SCALEBITS);
                    target->valid_params &= ~(1 << P_NUMBER);
                    target->params[P_SCALEBITS] = right_hap->params[P_SCALEBITS];
                    target->scale_bits = right_hap->scale_bits;
                } else if (param_idx == P_SCALEBITS && target->has_param(P_NOTE)) {
                    // apply a scale to a note -> quantize to scale
                    target->params[P_NOTE] =
                        quantize_note_to_scale(right_hap->scale_bits, right_hap->params[P_SCALEBITS], (int)target->params[P_NOTE]);
                    target->valid_params |= (1 << P_SCALEBITS);
                    target->params[P_SCALEBITS] = right_hap->params[P_SCALEBITS];
                    target->scale_bits = right_hap->scale_bits;
                } else { // just copy the value
                    target->params[param_idx] = right_hap->params[param_idx];
                    if (param_idx == P_SCALEBITS)
                        target->scale_bits = right_hap->scale_bits;
                    target->valid_params |= 1 << param_idx;
                }
            },
            P_NUMBER);
        break;
    }
    case N_OP_ADD:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, a, b, hapid, merge_repeated_leaves, nullptr, add_value_func, 0);
        break;
    case N_OP_SUB:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, a, b, hapid, merge_repeated_leaves, nullptr, add_value_func, 1);
        break;
    case N_OP_GAIN:
        param = P_GAIN;
        goto assign_value;
    case N_OP_NOTE:
        param = P_NOTE;
        goto assign_value;
    case N_OP_S:
        param = P_SOUND;
        goto assign_value;
    case N_OP_GATE:
        param = P_GATE;
        goto assign_value;
    case N_OP_ATTACK:
        param = P_A;
        goto assign_value;
    case N_OP_DECAY:
        param = P_D;
        goto assign_value;
    case N_OP_SUSTAIN:
        param = P_S;
        goto assign_value;
    case N_OP_RELEASE:
        param = P_R;
        goto assign_value;
    case N_OP_PAN:
        param = P_PAN;
        goto assign_value;
    assign_value:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, a, b, hapid, merge_repeated_leaves, nullptr, apply_value_func, param);
        break;
    case N_OP_CLIP:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, a, b, hapid, merge_repeated_leaves, nullptr,
            [](hap_t *target, hap_t *right_hap, size_t context) {
                float value = right_hap ? right_hap->get_param(P_NUMBER, 0.5f) : 0.5f;
                if (value <= 0.f)
                    target->valid_params = 0;
                else
                    target->t1 = target->t0 + (target->t1 - target->t0) * value;
            },
            0);
        break;
    case N_OP_DEGRADE: {
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, a, b, hapid, merge_repeated_leaves,
            [](hap_t *target, hap_t *right_hap, int new_hapid) {
                float value = right_hap ? right_hap->get_param(P_NUMBER, 0.5f) : 0.5f;
                if (value >= 1.f)
                    return 0;
                if (value <= 0.f)
                    return 1;
                return ((pcg_mix(pcg_next(new_hapid)) & 0xffffff) >= (value * 0xffffff)) ? 1 : 0;
            },
            nullptr, 0);
        break;
    }
    case N_FASTCAT:
        speed_scale = kids_total_length;
        // fall thru
    case N_CAT:
    case N_GRID: {
        if (kids_total_length <= 0.f || n->first_child < 0)
            break;
        hap_time child_a = a * speed_scale, child_b = b * speed_scale;
        int loopidx = floor(child_a / kids_total_length + hap_eps);
        hap_time from = loopidx * kids_total_length;
        hap_time loop_from = from;
        int childidx = 0;
        float lines_per_cycle = 1.f;
        if (n->type == N_GRID) {
            bfs_start_end[nodeidx].local_time_of_eval = child_a;
            lines_per_cycle = bfs_min_max_value[nodeidx].mn;
            if (lines_per_cycle < 1.f)
                break;
        }
        while (from < child_b) {
            int childnode = n->first_child + childidx;
            hap_time child_length = get_length(childnode);
            hap_time to = from + child_length;
            hap_time next_from = to;
            if (n->type == N_GRID) {
                hap_time grid_time_offset = bfs_grid_time_offset[childnode] / lines_per_cycle;
                hap_time next_grid_time_offset =
                    (childidx < n->num_children - 1) ? bfs_grid_time_offset[childnode + 1] / lines_per_cycle : kids_total_length;
                hap_time grid_time_first_loop = grid_time_offset + bfs_kids_total_length[childnode];
                from = loop_from + grid_time_offset;
                next_from = loop_from + next_grid_time_offset;
                // these two lines prevent the child from looping. not sure if I want it or not.
                // if (next_grid_time_offset > grid_time_first_loop)
                //     next_grid_time_offset = grid_time_first_loop;
                to = loop_from + next_grid_time_offset;
                if (from >= child_b)
                    break;
            }
            if (to <= from)
                break;
            if (to > child_a) {
                // we shift time so that the child feels like it's just looping on its own
                hap_time child_from = loopidx * child_length;
                hap_time child_to = (loopidx + 1) * child_length;
                hap_time tofs = from - child_from;
                // we pass down the query range scaled & shifted into child local time,
                // but we also clip it to the extent of this part of the cat (from-to)
                // in that way, large query ranges dont return multiple copies of the children
                // but also, small query ranges propagate down the callstack to keep things fast.
                hap_span_t newhaps =
                    _make_haps(dst, tmp_size, viz_time, childnode, max(child_a, from) - tofs, min(child_b, to) - tofs,
                               hash2_pcg(hapid, childidx + loopidx * n->num_children), merge_repeated_leaves);
                for (hap_t *src_hap = newhaps.s; src_hap < newhaps.e; src_hap++) {
                    if (src_hap->t1 > to - tofs)
                        src_hap->t1 = to - tofs;
                    src_hap->t0 = (src_hap->t0 + tofs) / speed_scale;
                    src_hap->t1 = (src_hap->t1 + tofs) / speed_scale;
                    if (src_hap->t0 > b || src_hap->t1 < a ||
                        src_hap->t0 >= src_hap->t1) { // filter out haps that are outside the query range
                        src_hap->valid_params = 0;
                    }
                }
            }
            if (++childidx == n->num_children) {
                childidx = 0;
                loopidx++;
                loop_from += kids_total_length;
            }
            from = next_from;
        }
        break;
    }
    case N_OP_ADSR: {
        if (n->num_children < 2)
            break;
        hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, a, b, hash2_pcg(hapid, n->first_child),
                                          merge_repeated_leaves);
        for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
            _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 1, nullptr, apply_value_func, P_A);
            if (n->num_children > 2)
                _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 2, nullptr, apply_value_func, P_D);
            if (n->num_children > 3)
                _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 3, nullptr, apply_value_func, P_S);
            if (n->num_children > 4)
               _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 4, nullptr, apply_value_func, P_R);
        }
        break;
    }
    case N_OP_EUCLID: {
        if (n->num_children < 3)
            break;
        // TODO make we can shorten this by making use of apply_steps repeatedly. maybe! but this is more explicit for now...
        hap_t tmp_mem[4 * tmp_size];
        hap_span_t tmp0 = {tmp_mem, tmp_mem + 1 * tmp_size};
        hap_span_t tmp1 = {tmp_mem + 1 * tmp_size, tmp_mem + 2 * tmp_size};
        hap_span_t tmp2 = {tmp_mem + 2 * tmp_size, tmp_mem + 3 * tmp_size};
        hap_span_t tmp3 = {tmp_mem + 3 * tmp_size, tmp_mem + 4 * tmp_size};
        hap_span_t left_haps =
            _make_haps(tmp0, tmp_size, viz_time, n->first_child, a, b, hash2_pcg(hapid, n->first_child), merge_repeated_leaves);
        hap_span_t setsteps_haps =
            _make_haps(tmp1, tmp_size, viz_time, n->first_child + 1, a, b, hash2_pcg(hapid, n->first_child + 1), true);
        hap_span_t numsteps_haps =
            _make_haps(tmp2, tmp_size, viz_time, n->first_child + 2, a, b, hash2_pcg(hapid, n->first_child + 2), true);
        hap_span_t rot_haps = {};
        if (n->num_children > 3)
            rot_haps = _make_haps(tmp3, tmp_size, viz_time, n->first_child + 3, a, b, hash2_pcg(hapid, n->first_child + 3), true);
        if (rot_haps.empty())
            rot_haps = {};
        for (hap_t *numsteps_hap = numsteps_haps.s; numsteps_hap < numsteps_haps.e; numsteps_hap++) {
            int numsteps = (int)numsteps_hap->get_param(P_NUMBER, 0.f);
            if (numsteps < 1)
                continue;
            hap_time child_a = a * numsteps;
            hap_time child_b = b * numsteps;
            for (int i = floor(child_a + hap_eps); i < child_b; i++) {
                hap_time t0 = i / (float)numsteps;
                hap_time t1 = (i + 1) / (float)numsteps;
                if (t0 >= b || t1 <= a)
                    continue;
                for (hap_t *setsteps_hap = setsteps_haps.s; setsteps_hap < setsteps_haps.e; setsteps_hap++) {
                    if (setsteps_hap->t0 > t0 || setsteps_hap->t1 <= t0)
                        continue;
                    int set_steps = (int)setsteps_hap->get_param(P_NUMBER, 0.f);
                    if (set_steps < 1)
                        continue;
                    for (hap_t *rot_hap = rot_haps.s; rot_hap < rot_haps.e || rot_haps.empty(); rot_hap++) {
                        int rot = rot_hap ? (int)rot_hap->get_param(P_NUMBER, 0.f) : 0;
                        if (euclid_rhythm(i, set_steps, numsteps, rot)) {
                            for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
                                if (dst.s >= dst.e)
                                    break;
                                if (left_hap->t0 > t0 || left_hap->t1 <= t0)
                                    continue;
                                *dst.s = *left_hap;
                                dst.s->t0 = t0;
                                dst.s->t1 = t1;
                                dst.s->hapid = hash2_pcg(hapid, left_hap->hapid);
                                dst.s++;
                            } // left haps
                        }
                        if (!rot_hap)
                            break;
                    } // rot haps
                } // setsteps haps
            } // looping over subdivisions
        } // numsteps haps
    } // euclid
    } // switch
        rv.e = dst.s;
        return rv;
    } // make_haps

    void pretty_print_haps(hap_span_t haps, hap_time from, hap_time to) {
        for (hap_t *hap = haps.s; hap < haps.e; hap++) {
            // hap onset must overlap the query range
            if (hap->t0 < from || hap->t0 >= to)
                continue;
            if (hap->valid_params == 0) {
                printf(COLOR_GREY "%08x(%d) %5.2f -> %5.2f (filtered)\n" COLOR_RESET, hap->hapid, hap->node, hap->t0, hap->t1);
                continue;
            }
            printf(COLOR_GREY "%08x(%d) " COLOR_CYAN "%5.2f" COLOR_BLUE " -> %5.2f " COLOR_RESET, hap->hapid, hap->node, hap->t0,
                   hap->t1);
            for (int i = 0; i < P_LAST; i++) {
                if (hap->valid_params & (1 << i)) {
                    if (i == P_SOUND) {
                        Sound *sound = get_sound_by_index((int)hap->params[i]);
                        printf("s=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", sound ? sound->name : "<missing>");
                    } else if (i == P_NOTE) {
                        printf("note=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", print_midinote((int)hap->params[i]));
                    } else {
                        printf("%s=" COLOR_BRIGHT_YELLOW "%5.2f" COLOR_RESET " ", param_names[i], hap->params[i]);
                    }
                }
            }
            printf("\n");
        }
    }
