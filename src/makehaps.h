// code for turning patterns (a tree of nodes) into haps (a flat list of events with times and values)

void merge_hap(hap_t *dst, hap_t *src) {
    int p = src->valid_params;
    dst->valid_params |= p;
    while (p) {
        int i = __builtin_ctz(p);
        dst->params[i] = src->params[i];
        p &= ~(1 << i);
    }
    if (src->has_param(P_SCALEBITS))
        dst->scale_bits = src->scale_bits;
}

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

float pattern_t::get_length(int nodeidx) {
    if (nodeidx < 0)
        return 0.f;
    int t = bfs_nodes[nodeidx].type;
    if (t == N_OP_REPLICATE || t == N_OP_ELONGATE || t == N_GRID)
        return bfs_min_max_value[nodeidx].mx; // use max value as length
    return 1.f;
}

bool pattern_t::_append_number_hap(hap_span_t &dst, int nodeidx, int hapid, float value) {
    if (dst.s >= dst.e || nodeidx < 0)
        return false;
    hap_t *hap = dst.s++;
    hap->t0 = -1e9;
    hap->t1 = 1e9;
    hap->node = nodeidx;
    hap->hapid = hapid;
    hap->valid_params = 1 << P_NUMBER;
    hap->params[P_NUMBER] = value;
    hap->scale_bits = 0;
    return true;
}

bool pattern_t::_append_leaf_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid) {
    if (dst.s >= dst.e || t0 > t1 || nodeidx < 0)
        return false;
    bfs_node_t *n = bfs_nodes + nodeidx;
    int value_type = n->value_type;
    if (value_type <= VT_NONE)
        return false;
    float_minmax_t minmax = bfs_min_max_value[nodeidx];
    float v;
    if (minmax.mn != minmax.mx && value_type != VT_SCALE) { // randomize in range
        float t = (pcg_mix(pcg_next(hapid)) & 0xffffff) * (1.f / 0x1000000);
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

void pattern_t::_filter_haps(hap_span_t left_haps, hap_time speed_scale, hap_time tofs, hap_time when) {
    for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
        left_hap->t0 = (left_hap->t0 + tofs) / speed_scale;
        left_hap->t1 = (left_hap->t1 + tofs) / speed_scale;
        if (left_hap->t0 > when || left_hap->t1 < when) {
            left_hap->valid_params = 0;
        }
    }
}

int pattern_t::_apply_values(hap_span_t &dst, int tmp_size, float viz_time, hap_t *structure_hap, int value_node_idx,
                             filter_cb_t filter_cb, value_cb_t value_cb, size_t context, hap_time when, int num_rhs) {
    hap_time structure_t0 = when; //structure_hap->t0;
    // structure_t0 = (t0+t1)*0.5;
    hap_t tmp_mem[tmp_size * num_rhs];
    int new_hap_id = hash2_pcg(structure_hap->hapid, value_node_idx + (int)(structure_t0 /* * 1000.f */));
    hap_span_t value_haps[num_rhs];
    for (int i = 0; i < num_rhs; i++) {
        hap_span_t tmp = {tmp_mem + i * tmp_size, tmp_mem + (i + 1) * tmp_size};
        value_haps[i] = _make_haps(tmp, tmp_size, viz_time, value_node_idx + i, structure_t0, new_hap_id + i * 23);
    }
    int count = 0;
    int structure_hapid = structure_hap->hapid;
    // COMMENT: we just take the structure from the left, but also only loop over the first parameter's haps,
    // and just take the later params 'modulo first param count'. which is wrong but simple.
    int num_value_haps = value_haps[0].e - value_haps[0].s;
    if (num_value_haps == 0)
        num_value_haps = 1;
    for (int value_hap_idx = 0; value_hap_idx < num_value_haps; value_hap_idx++) {
        hap_t *value_hap_i[num_rhs];
        for (int i = 0; i < num_rhs; i++) {
            int n = value_haps[i].e - value_haps[i].s;
            if (!n)
                value_hap_i[i] = nullptr;
            else
                value_hap_i[i] = value_haps[i].s + (value_hap_idx % n);
        }
        hap_t *right_hap = value_hap_i[0]; // TODO: more than 1
        if (right_hap && !right_hap->valid_params)
            continue;
        int new_hapid = hash2_pcg(structure_hapid, right_hap ? right_hap->hapid : 0);
        int num_copies = filter_cb ? filter_cb(structure_hap, right_hap, new_hapid, when) : 1;
        for (int i = 0; i < num_copies; i++) {
            hap_t *target = structure_hap;
            if (!target->valid_params)
                continue;
            if (count++) {
                if (dst.s >= dst.e)
                    break;
                target = dst.s++;
                *target = *structure_hap;
                target->hapid = new_hapid;
            }
            if (value_cb) {
                if (!value_cb(target, value_hap_i, context + i, when)) {
                    // cancel this copy!
                    count--;
                    if (count > 0)
                        --dst.s;
                }
            }
        }
    }
    if (count == 0)
        structure_hap->valid_params = 0; // no copies wanted!
    return count;
}

void pattern_t::_apply_unary_op(hap_span_t &dst, int tmp_size, float viz_time, int parent_idx, hap_time when, int parent_hapid,
                                filter_cb_t filter_cb, value_cb_t value_cb, size_t context, int num_rhs) {
    bfs_node_t *n = bfs_nodes + parent_idx;
    if (n->num_children < 1)
        return;
    int right_child = (n->num_children > 1) ? n->first_child + 1 : -1;
    hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, when, hash2_pcg(parent_hapid, n->first_child));
    for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
        _apply_values(dst, tmp_size, viz_time, left_hap, right_child, filter_cb, value_cb, context, when, num_rhs);
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

int apply_value_func(hap_t *target, hap_t **right_hap, size_t param_idx, hap_time when) {
    if (!right_hap[0] || param_idx >= P_LAST)
        return 1;
    int src_idx = param_idx;
    if (!right_hap[0]->has_param(src_idx)) {
        if (!right_hap[0]->has_param(P_NUMBER))
            return 1;
        src_idx = P_NUMBER;
    }
    target->params[param_idx] = right_hap[0]->params[src_idx];
    target->valid_params |= 1 << param_idx;
    return 1;
}

int apply_fit_func(hap_t *left_hap, hap_t **right_hap, size_t param_idx, hap_time when) {
    if (!right_hap || param_idx >= P_LAST)
        return 1;
    if (!right_hap[0]->has_param(P_NUMBER))
        return 1;
    float n = right_hap[0]->params[P_NUMBER];
    if (n <= 0.f)
        n = 1.f;
    if (left_hap->has_param(P_SOUND) && G->dt != 0.) {
        wave_t *w = get_wave(get_sound_by_index(left_hap->params[P_SOUND]), 0);
        if (w->num_frames) {
            float ratio = w->num_frames / (((left_hap->t1 - left_hap->t0) / G->dt * n) / SAMPLE_RATE * w->sample_rate);
            float note = log2(ratio) * 12 + C3;
            left_hap->params[P_NOTE] = note;
            left_hap->valid_params |= 1 << P_NOTE;
        }
    }
    return 1;
}

int add_value_func(hap_t *target, hap_t **right_hap, size_t negative, hap_time when) { // param_idx 1=sub
    if (!right_hap || !right_hap[0]->has_param(P_NUMBER) || !target->valid_params)
        return 1;
    int param_idx = __builtin_ctz(target->valid_params);
    float v = right_hap[0]->params[P_NUMBER];
    target->params[param_idx] += negative ? -v : v;
    return 1;
}


float pattern_t::compute_blendnear_weights(bfs_node_t *n, float *weights) {
    float tot_weight = 0.f;
    for (int i = 0; i < n->num_children; i++) {
        // use the second closest to any other pattern to scale the distances to this node.
        float closest=1e9, second_closest=1e9;
        int pidx = (int)bfs_min_max_value[n->first_child + i].mx;
        if (pidx<0 || pidx>=stbds_shlen(G->patterns_map)) {
            weights[i] = 0.f;
        } else {
            pattern_t *pati = &G->patterns_map[pidx];
            for (int j=0; j<n->num_children; j++) if (j!=i) {
                int pidx = (int)bfs_min_max_value[n->first_child + j].mx;
                if (pidx<0 || pidx>=stbds_shlen(G->patterns_map)) {
                    continue;
                }
                pattern_t *patj = &G->patterns_map[pidx];
                float dsq = square(pati->x - patj->x) + square(pati->y - patj->y);
                if (dsq < closest) {
                    //second_closest = closest;
                    closest = dsq;
                } 
                // else if (dsq < second_closest) {
                //     second_closest = dsq;
                // }
            }
            // if (second_closest == 1e9) second_closest = closest;
            // if (!second_closest) second_closest = 1.f;
            if (!closest) closest = 1.f;
            float dsq = (square(pati->x - x) + square(pati->y - y)) / closest;
            weights[i] = expf(-dsq*3.f); // smoothstep(1.f, 0.f, dsq);
            if (weights[i] < 0.01f) weights[i] = 0.f;
            tot_weight += weights[i];
        }
    }
    return tot_weight;
}

hap_span_t pattern_t::_make_haps(hap_span_t &dst, int tmp_size, float viz_time, int nodeidx, hap_time when, int hapid) {
    if (nodeidx < 0 || !bfs_nodes)
        return {};
    hap_span_t rv = {dst.s, dst.s};
    if (dst.s >= dst.e)
        return rv;
    bfs_node_t *n = bfs_nodes + nodeidx;
    hap_time speed_scale = 1.f;
    hap_time kids_total_length = bfs_kids_total_length[nodeidx];
    int param = P_NUMBER;
    bool appended = false;
    switch (n->type) {
    case N_CALL: {
        int patidx = (int)bfs_min_max_value[nodeidx].mx;
        if (patidx >= 0 && patidx < stbds_shlen(G->patterns_map)) {
            pattern_t *pat = &G->patterns_map[patidx];
            hap_span_t hs = pat->_make_haps(dst, tmp_size, viz_time, 0, when, hapid);
            if (hs.e > dst.s) {
                appended = true;
            }
        }
        break;
    }
    case N_UP:
        appended = _append_number_hap(dst, nodeidx, hapid, frac(when));
        break;
    case N_DOWN:
        appended = _append_number_hap(dst, nodeidx, hapid, 1.f-frac(when));
        break;
    case N_UPDOWN: {
        float f= frac(when);
        appended = _append_number_hap(dst, nodeidx, hapid, (f<0.5f) ? f*2.f : 2.f-f*2.f);
        break;}
    case N_DOWNUP: {
        float f= frac(when);
        appended = _append_number_hap(dst, nodeidx, hapid, (f<0.5f) ? 1.f-f*2.f : f*2.f-1.f);
        break;}
    case N_NEAR: {
        int pidx = (int)bfs_min_max_value[nodeidx].mx;
        if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) {
            pattern_t *pat = &G->patterns_map[pidx];
            appended = _append_number_hap(dst, nodeidx, hapid, pat->get_near_output(this));
        }
        break;
    }
    case N_COLOR: {
        float c = get_color_output((int)bfs_min_max_value[nodeidx].mx);
        appended = _append_number_hap(dst, nodeidx, hapid, c);
        break;
    }
    case N_SIN:
        appended = _append_number_hap(dst, nodeidx, hapid, sinf(when * PI * 2) * 0.5f + 0.5f);
        break;
    case N_SIN2:
        appended = _append_number_hap(dst, nodeidx, hapid, sinf(when * PI * 2));
        break;
    case N_COS:
        appended = _append_number_hap(dst, nodeidx, hapid, cosf(when * PI * 2) * 0.5f + 0.5f);
        break;
    case N_COS2:
        appended = _append_number_hap(dst, nodeidx, hapid, cosf(when * PI * 2));
        break;
    case N_RAND:
        appended =
            _append_number_hap(dst, nodeidx, hapid, (pcg_mix(hash2_pcg(hapid, (int)floor(when) )) & 0xffff) / 65536.f);
        break;
    case N_RAND2:
        appended = _append_number_hap(dst, nodeidx, hapid,
                                      (pcg_mix(hash2_pcg(hapid, (int)floor(when) )) & 0xffff) / 32768.f - 1.f);
        break;
    case N_CC: {
        int cc = (int)(bfs_min_max_value[nodeidx].mx)&15;
        appended = _append_number_hap(dst, nodeidx, hapid, G->midi_cc[16 + cc] / 127.f);
        break; }
    case N_RANDI: {
        hap_t tmp_mem[tmp_size];
        hap_span_t tmp = {tmp_mem, tmp_mem + tmp_size};
        hap_span_t right_haps = _make_haps(tmp, tmp_size, viz_time, n->first_child, when, hash2_pcg(hapid, n->first_child));
        for (hap_t *right_hap = right_haps.s; right_hap < right_haps.e; right_hap++) {
            int limit = (int)right_hap->get_param(P_NUMBER, 0.f);
            int r = (pcg_mix(hash2_pcg(right_hap->hapid, (int)floor(when))) & 0xffff);
            r = limit ? (r % limit) : 0;
            appended |= _append_number_hap(dst, nodeidx, hapid, r);
        }
        break;

        break;
    }

    case N_CURVE: {
        int ncurve = stbds_arrlen(curvedata);
        int idx0 = clamp((int)bfs_min_max_value[nodeidx].mn, 0, ncurve);
        int idx1 = clamp((int)bfs_min_max_value[nodeidx].mx, 0, ncurve);
        int nidx = idx1-idx0;
        bfs_start_end[nodeidx].local_time_of_eval = frac(when);
        if (nidx>0) {
            float t = frac(when) * nidx;
            int it = (int)floorf(t);
            int it2 = (it+1) % nidx;
            float v = lerp(curvedata[it + idx0], curvedata[it2 + idx0], t - it);
            appended = _append_number_hap(dst, nodeidx, hapid, v);
        }
        break;
    }
    case N_LEAF: {
        float f = floor(when);
        appended = _append_leaf_hap(dst, nodeidx, f, f+1.f, hash2_pcg(hapid, (int)when));
        break;
    }
    case N_POLY:
        speed_scale = (bfs_min_max_value[nodeidx].mx > 0) ? bfs_min_max_value[nodeidx].mx
                      : (n->first_child >= 0)             ? bfs_kids_total_length[n->first_child]
                                                          : 1.;
    case N_PARALLEL: {
        if (speed_scale <= 0.f)
            break;
        for (int childidx = 0; childidx < n->num_children; ++childidx) {
            int child = n->first_child + childidx;
            _make_haps(dst, tmp_size, viz_time, child, when * speed_scale, hash2_pcg(hapid, childidx));
        }
        _filter_haps({rv.s, dst.s}, speed_scale, 0., when);
        break;
    }
    case N_OP_FIT: {
        hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, when, hapid);
        for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
            if (left_hap->has_param(P_SOUND) && G->dt != 0.) {
                wave_t *w = get_wave(get_sound_by_index(left_hap->params[P_SOUND]), 0);
                if (w->num_frames) {
                    float ratio = w->num_frames / (((left_hap->t1 - left_hap->t0) / G->dt) / SAMPLE_RATE * w->sample_rate);
                    float note = log2(ratio) * 12 + C3;
                    left_hap->params[P_NOTE] = note;
                    left_hap->valid_params |= 1 << P_NOTE;
                }
            }
        }
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
                hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, when * speed_scale,
                                                  hash2_pcg(hapid, n->first_child));
                _filter_haps(left_haps, speed_scale, 0., when);
            }
            break;
        }
        hap_t tmp_mem[tmp_size];
        hap_span_t tmp = {tmp_mem, tmp_mem + tmp_size};
        hap_span_t right_haps =
            _make_haps(tmp, tmp_size, viz_time, n->first_child + 1, when, hash2_pcg(hapid, n->first_child + 1));
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
            case N_OP_DIVIDE:
                speed_scale = num ? 1. / num : 0.;
                break;
            }
            if (speed_scale <= 0.f)
                continue;
            hap_span_t left_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, when * speed_scale, hapid); // this used to include right hapid, but it means rand inside a *4 with offset for smooth broke... 
//                                              hash2_pcg(hapid, right_hap->hapid));
            _filter_haps(left_haps, speed_scale, tofs, when);
        }
        break;
    }
    case N_RANDOM: {
        if (n->num_children <= 0)
            break;
        int i = floor(when);
        int childidx = (hash2_pcg(hapid, i)) % n->num_children;
        _make_haps(dst, tmp_size, viz_time, n->first_child + childidx, when, hash2_pcg(hapid, i));
        break;
    }

    // special case for N_P_NUMBER
    n_p_number:
    //case N_P_NUMBER: 
    { // take structure from the left; value(s) from the right. copy as needed
        if (n->num_children < 2)
            break;
        // the : operator is quite flexible for applying scales to notes or numbers.
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, nodeidx, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t param_idx, hap_time when) {
                if (!right_hap[0] || !right_hap[0]->valid_params)
                    return 0;
                int right_params = right_hap[0]->valid_params;
                while (right_params) {
                    int param_idx = __builtin_ctz(right_params);
                    right_params &= ~(1 << param_idx);
                    if (param_idx == P_NUMBER && target->has_param(P_SCALEBITS) && !target->has_param(P_NOTE)) {
                        // apply a number to a scale -> index scale into a note
                        target->params[P_NOTE] =
                            scale_index_to_note(target->scale_bits, target->params[P_SCALEBITS], (int)right_hap[0]->params[P_NUMBER]);
                        target->valid_params |= 1 << P_NOTE;
                    } else if (param_idx == P_SCALEBITS && target->has_param(P_NUMBER) && !target->has_param(P_NOTE)) {
                        // apply a scale to a number -> index scale into a note
                        target->params[P_NOTE] = scale_index_to_note(right_hap[0]->scale_bits, right_hap[0]->params[P_SCALEBITS],
                                                                    (int)target->params[P_NUMBER]);
                        target->valid_params |= (1 << P_NOTE) | (1 << P_SCALEBITS);
                        target->valid_params &= ~(1 << P_NUMBER);
                        target->params[P_SCALEBITS] = right_hap[0]->params[P_SCALEBITS];
                        target->scale_bits = right_hap[0]->scale_bits;
                    } else if (param_idx == P_SCALEBITS && target->has_param(P_NOTE)) {
                        // apply a scale to a note -> quantize to scale
                        target->params[P_NOTE] = quantize_note_to_scale(right_hap[0]->scale_bits, right_hap[0]->params[P_SCALEBITS],
                                                                        (int)target->params[P_NOTE]);
                        target->valid_params |= (1 << P_SCALEBITS);
                        target->params[P_SCALEBITS] = right_hap[0]->params[P_SCALEBITS];
                        target->scale_bits = right_hap[0]->scale_bits;
                    } else { // just copy the value
                        target->params[param_idx] = right_hap[0]->params[param_idx];
                        if (param_idx == P_SCALEBITS)
                            target->scale_bits = right_hap[0]->scale_bits;
                        target->valid_params |= 1 << param_idx;
                    }
                }
                return 1;
            },
            P_NUMBER);
        break;
    }
    case N_BLEND: case N_BLENDNEAR: {
        bool is_blendnear = n->type == N_BLENDNEAR;
        int num_weights = (is_blendnear) ? n->num_children : (n->num_children + 1)/2;
        if (!num_weights) break;
        float weights[num_weights];
        // if the number of children is odd, we 'need a zero weight' meaning that we compute that as 1-totweight automatically
        int need_zero_weight = n->num_children & 1;
        float tot_weight = 0.f;
        if (is_blendnear) {
            tot_weight = compute_blendnear_weights(n, weights);
        } else {
            for (int i = need_zero_weight; i < num_weights; i++) {
                int childidx = i*2-need_zero_weight;
                hap_t tmp_mem[tmp_size];
                hap_span_t tmp = {tmp_mem, tmp_mem + tmp_size};
                hap_span_t hs = _make_haps(tmp, tmp_size, viz_time, n->first_child + childidx, when, hash2_pcg(hapid, n->first_child + childidx));
                weights[i] = 0.f;
                for (hap_t *h = hs.s; h < hs.e; h++) 
                    weights[i] = max(weights[i], h->get_param(P_NUMBER, weights[i]));
                tot_weight += weights[i];
            }
            if (need_zero_weight) {
                weights[0] = max(1.f - tot_weight, 0.f);
                tot_weight += weights[0];
            }
        }
        hap_t tmp_mem[tmp_size * num_weights];
        hap_span_t tmp[num_weights];
        int hash = 0;
        bool has_sound_or_note = false;
        int max_poly = 0;
        for (int i = 0; i < num_weights; ++i) {
            if (weights[i] <= 0.) {
                tmp[i] = {NULL, NULL};
                continue;
            }
            int childidx = (is_blendnear) ? i : (i*2+ !need_zero_weight);
            tmp[i] = {tmp_mem + i * tmp_size, tmp_mem + (i + 1) * tmp_size};
            tmp[i] = _make_haps(tmp[i], tmp_size, viz_time, n->first_child + childidx, when, hash2_pcg(hapid, n->first_child + childidx));
            max_poly = max(max_poly, (int)(tmp[i].e - tmp[i].s));
            // if it contains sound or note, update the hash with that hap
            // then select between them
            for (hap_t *hap = tmp[i].s; hap < tmp[i].e; hap++) {
                if (hap->has_param(P_SOUND) || hap->has_param(P_NOTE)) {
                    has_sound_or_note = true;
                    hash = hash2_pcg(hash, hap->hapid);
                }
            }
        }
        if (has_sound_or_note) {
            // pick an index from 0 to numweights based on the cdf of weights
            float pick = ((hash & 0xffffff)*tot_weight) / 0x1000000;
            int i;
            for (i = 0; i < num_weights; i++) {
                bool picked = pick < weights[i] || i == num_weights-1;
                if (is_blendnear) {
                    int pidx = (int)bfs_min_max_value[n->first_child + i].mx;
                    if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) G->patterns_map[pidx].picked = picked;
                }
                if (picked) break;
                pick -= weights[i];
            }
            if (is_blendnear) 
            for (int j= i+1; j< num_weights; j++) {
                int pidx = (int)bfs_min_max_value[n->first_child + j].mx;
                if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) G->patterns_map[pidx].picked = 0.f;
            }
            hap_span_t src = tmp[i];
            for (hap_t *src_hap = src.s; src_hap < src.e; src_hap++) {
                if (dst.s >= dst.e)
                        break;
                hap_t *target = dst.s++;
                *target = *src_hap;
            }
        } else {
            // blend all the haps together according to the weights
            float tot_weights[max_poly][P_LAST];
            if (dst.s + max_poly > dst.e)
                max_poly = (int)(dst.e - dst.s);
            hap_span_t out = {dst.s, dst.s + max_poly};
            dst.s+=max_poly;
            for (int i=0; i< max_poly; ++i) out.s[i].valid_params = 0;
            for (int i =0; i < num_weights; ++i) {
                if (is_blendnear) {
                    int pidx = (int)bfs_min_max_value[n->first_child + i].mx;
                    if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) G->patterns_map[pidx].picked = weights[i];
                }
                if (tmp[i].empty()) continue;
                hap_t *src_hap = tmp[i].s;                
                for (int j = 0; j<max_poly; ++j) {
                    int valid = src_hap->valid_params;
                    while (valid) {
                        int bit = __builtin_ctz(valid);
                        valid &= ~(1 << bit);
                        float v = src_hap->params[bit];
                        if (!out.s[j].has_param(bit)) {
                            tot_weights[j][bit] = 0.f;
                            out.s[j].params[bit] = 0.f;
                            out.s[j].valid_params |= 1 << bit;
                        }
                        out.s[j].params[bit] += v * weights[i];
                        tot_weights[j][bit] += weights[i];
                    } // loop over valid src params
                    src_hap++;
                    if (src_hap == tmp[i].e) src_hap=tmp[i].s;
                } // loop over output haps
            } // loop over weights
            // normalize:
            for (int j = 0; j<max_poly; ++j) {
                int valid = out.s[j].valid_params;
                while (valid) {
                    int bit = __builtin_ctz(valid);
                    valid &= ~(1 << bit);
                    float tw = tot_weights[j][bit];
                    if (tw > 0.f)
                        out.s[j].params[bit] /= tw;
                }
            }
        }
        break;
    }
    /*
    case N_OP_BLEND: {
        hap_t tmp_mem[tmp_size*3];
        hap_span_t tmp0 = {tmp_mem, tmp_mem + 1 * tmp_size};
        hap_span_t tmp1 = {tmp_mem + 1 * tmp_size, tmp_mem + 2 * tmp_size};
        hap_span_t tmp2 = {tmp_mem + 2 * tmp_size, tmp_mem + 3 * tmp_size};
        hap_span_t left = _make_haps(tmp0, tmp_size, viz_time, n->first_child, when, hash2_pcg(hapid, n->first_child));
        hap_span_t right = _make_haps(tmp1, tmp_size, viz_time, n->first_child + 2, when, hash2_pcg(hapid, n->first_child + 2));
        hap_span_t prob = _make_haps(tmp2, tmp_size, viz_time, n->first_child + 1, when, hash2_pcg(hapid, n->first_child + 1));
        int hash = 0;
        for (hap_t *left_hap = left.s; left_hap < left.e; left_hap++) hash = hash2_pcg(hash, left_hap->hapid);
        for (hap_t *right_hap = right.s; right_hap < right.e; right_hap++) hash = hash2_pcg(hash, right_hap->hapid);
        float hash_prob = (hash&0xffffff) / 16777216.f;
        float thresh = prob.empty() ? 0.5f : prob.s->get_param(P_NUMBER, 0.5f);
        hap_span_t src = (hash_prob > thresh) ? left : right;
        for (hap_t *src_hap = src.s; src_hap < src.e; src_hap++) {
            if (dst.s >= dst.e)
                break;
            hap_t *target = dst.s++;
            *target = *src_hap;
        }
        break;
    }
        */
    case N_OP_RIBBON: {
        hap_t tmp_mem[tmp_size*2];
        hap_span_t tmp0 = {tmp_mem, tmp_mem + 1 * tmp_size};
        hap_span_t tmp1 = {tmp_mem + 1 * tmp_size, tmp_mem + 2 * tmp_size};
        hap_span_t cycle = _make_haps(tmp0, tmp_size, viz_time, n->first_child + 1, when, hash2_pcg(hapid, n->first_child + 1));
        hap_span_t num_cycles = _make_haps(tmp1, tmp_size, viz_time, (n->num_children > 2) ? n->first_child + 2 : -1, when, hash2_pcg(hapid, n->first_child + 2));
        for (hap_t *cycle_hap = cycle.s; cycle_hap < cycle.e; cycle_hap++) {
            float num_cycles_value = num_cycles.empty() ? 1.f : num_cycles.s->get_param(P_NUMBER, 1.f);
            if (num_cycles_value > 0.f) {
                hap_time child_when = fmodf(when, num_cycles_value) + cycle_hap->get_param(P_NUMBER, 0.f);
                hap_span_t child_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, child_when, hash2_pcg(hapid, n->first_child));
                for (hap_t *child_hap = child_haps.s; child_hap < child_haps.e; child_hap++) {
                    child_hap->t0 += (when-child_when);
                    child_hap->t1 += (when-child_when);
                }
            }
        }
        break; }
    case N_OP_EASE:
    case N_OP_SMOOTH: {
        // eval at when and when+1, then blend between them.
        hap_t tmp_mem[tmp_size*2];
        hap_span_t tmp0 = {tmp_mem, tmp_mem + 1 * tmp_size};
        hap_span_t tmp1 = {tmp_mem + 1 * tmp_size, tmp_mem + 2 * tmp_size};
        bool is_ease = n->type == N_OP_EASE;
        hap_time t = is_ease ? when : (when-0.5);
        hap_span_t out = _make_haps(dst, tmp_size, viz_time, n->first_child, t, hapid);
        hap_span_t blendwith = _make_haps(tmp0, tmp_size, viz_time, n->first_child, t + 1., hapid);
        hap_span_t power = _make_haps(tmp1, tmp_size, viz_time, (n->num_children > 1) ?  n->first_child + 1 : -1, when, hash2_pcg(hapid, n->first_child + 1));
        float power_value = power.empty() ? 1.f : expf((is_ease ? -1.f : 1.f) *power.s->get_param(P_NUMBER, 0.f));
        t=frac(t);
        if (is_ease) t = powf(t, power_value); else {
            t = 0.5f * ((t<0.5f) ? powf(t*2.f, power_value) : 2.f-powf((2.f-t*2.f), power_value));
        }
        const hap_t *srchap = blendwith.s;
        for (hap_t *out_hap = out.s; out_hap < out.e; out_hap++) {
            if (!is_ease) {
                out_hap->t0 += 0.5;
                out_hap->t1 += 0.5;
            }
            if (srchap < blendwith.e) {
                int shared_params = srchap->valid_params & out_hap->valid_params;
                while (shared_params) {
                    int bit = __builtin_ctz(shared_params);
                    out_hap->params[bit] = out_hap->params[bit] * (1.0f - t) + srchap->params[bit] * t;
                    shared_params &= ~(1 << bit);
                }
            }
        }
        break;
    }
    case N_OP_RANGE2:
    case N_OP_RANGE: {
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                if (!right_hap[0] || !right_hap[0]->has_param(P_NUMBER) || !right_hap[1] || !right_hap[1]->has_param(P_NUMBER) ||
                    !target->valid_params)
                    return 1;
                int param_idx = __builtin_ctz(target->valid_params);
                float mn = right_hap[0]->params[P_NUMBER];
                float mx = right_hap[1]->params[P_NUMBER];
                float v = target->params[param_idx];
                if (context == N_OP_RANGE2)
                    v = v * 0.5f + 0.5f;
                target->params[param_idx] = v * (mx - mn) + mn;
                return 1;
            },
            n->type, 2);
        break;
    }
    case N_OP_MUL:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                if (!right_hap[0] || !right_hap[0]->has_param(P_NUMBER))
                    return 1;
                int param_idx = __builtin_ctz(target->valid_params);
                float v = right_hap[0]->params[P_NUMBER];
                target->params[param_idx] *= v;
                return 1;
            },
            0);
        break;
    case N_OP_POW:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                if (!right_hap[0] || !right_hap[0]->has_param(P_NUMBER))
                    return 1;
                int param_idx = __builtin_ctz(target->valid_params);
                float v = right_hap[0]->params[P_NUMBER];
                target->params[param_idx] = powf(target->params[param_idx], v);
                return 1;
            },
            0);
        break;
    case N_OP_ROUND:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                int param_idx = __builtin_ctz(target->valid_params);
                target->params[param_idx] = roundf(target->params[param_idx]);
                return 1;
            },
            0);
        break;
    case N_OP_FLOOR:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                int param_idx = __builtin_ctz(target->valid_params);
                target->params[param_idx] = floorf(target->params[param_idx]);
                return 1;
            },
            0);
        break;
    case N_OP_DIV:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) { // param_idx 1=sub
                if (!right_hap[0] || !right_hap[0]->has_param(P_NUMBER) || !target->valid_params)
                    return 1;
                int param_idx = __builtin_ctz(target->valid_params);
                float v = right_hap[0]->params[P_NUMBER];
                target->params[param_idx] /= v;
                return 1;
            },
            0);
        break;
    case N_OP_ADD:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr, add_value_func, 0);
        break;
    case N_OP_SUB:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr, add_value_func, 1);
        break;
    case N_LAMBDA:
    break;
    case N_OP_NEVER:
        break;
    case N_OP_SOMETIMES:
    case N_OP_RARELY:
    case N_OP_OFTEN:
    case N_OP_ALWAYS:
    break;
    #define X(x, str, ...) case N_##x: param = x; goto assign_value;
    #include "params.h"
    assign_value:
        if (param == P_NUMBER)
            goto n_p_number;
        if (n->num_children ==1 ) {
            // we are using 'param name number' as a value, ie just create a hap with only that in it.
            hap_span_t new_haps = _make_haps(dst, tmp_size, viz_time, n->first_child, when, hapid);
            for (hap_t *new_hap = new_haps.s; new_hap < new_haps.e; new_hap++) {
                if (new_hap->valid_params & (1<<P_NUMBER)) {
                    new_hap->params[param] = new_hap->params[P_NUMBER];
                    new_hap->valid_params = (new_hap->valid_params & ~(1<<P_NUMBER)) | (1 << param);
                }
            }
            break;
        }
        else
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr, apply_value_func, param);
        break;
    case N_OP_FITN:
        _apply_unary_op(dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr, apply_fit_func, 0);
        break;

    case N_OP_CLIP:
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, nullptr,
            [](hap_t *target, hap_t **right_hap, size_t context, hap_time when) {
                float value = right_hap[0] ? right_hap[0]->get_param(P_NUMBER, 0.5f) : 0.5f;
                if (value <= 0.f)
                    target->valid_params = 0;
                else
                    target->t1 = target->t0 + (target->t1 - target->t0) * value;
                return 1;
            },
            0);
        break;
    case N_OP_DEGRADE: {
        _apply_unary_op(
            dst, tmp_size, viz_time, nodeidx, when, hapid, 
            [](hap_t *target, hap_t *right_hap, int new_hapid, hap_time when) {
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
        hap_time child_when = when * speed_scale;
        int loopidx = floor(when / kids_total_length);
        hap_time from = loopidx * kids_total_length;
        hap_time loop_from = from;
        int childidx = 0;
        float lines_per_cycle = 1.f;
        if (n->type == N_GRID) {
            bfs_start_end[nodeidx].local_time_of_eval = child_when;
            lines_per_cycle = bfs_min_max_value[nodeidx].mn;
            if (lines_per_cycle < 1.f)
                break;
        }
        while (from <= child_when) {
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
                if (from > child_when)
                    break;
            }
            if (to <= from)
                break;
            if (to > child_when) {
                // we shift time so that the child feels like it's just looping on its own
                hap_time child_from = loopidx * child_length;
                hap_time child_to = (loopidx + 1) * child_length;
                hap_time tofs = from - child_from;
                // we pass down the query range scaled & shifted into child local time,
                // but we also clip it to the extent of this part of the cat (from-to)
                // in that way, large query ranges dont return multiple copies of the children
                // but also, small query ranges propagate down the callstack to keep things fast.
                hap_span_t newhaps =
                    _make_haps(dst, tmp_size, viz_time, childnode, child_when - tofs,
                               hash2_pcg(hapid, childidx + loopidx * n->num_children));
                for (hap_t *src_hap = newhaps.s; src_hap < newhaps.e; src_hap++) {
                    if (src_hap->t1 > to - tofs)
                        src_hap->t1 = to - tofs;
                    src_hap->t0 = (src_hap->t0 + tofs) / speed_scale;
                    src_hap->t1 = (src_hap->t1 + tofs) / speed_scale;
                    if (src_hap->t0 > when || src_hap->t1 < when ||
                        src_hap->t0 > src_hap->t1) { // filter out haps that are outside the query range
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
    case N_OP_ADSR: 
    case N_OP_ADSR2:
    {
        if (n->num_children < 2)
            break;
        hap_span_t left_haps =
            _make_haps(dst, tmp_size, viz_time, n->first_child, when, hash2_pcg(hapid, n->first_child));
            bool is_adsr2 = n->type == N_OP_ADSR2;
        for (hap_t *left_hap = left_haps.s; left_hap < left_haps.e; left_hap++) {
            _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 1, nullptr, apply_value_func, is_adsr2 ? P_ATT2 : P_ATT, when);
            if (n->num_children > 2)
                _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 2, nullptr, apply_value_func, is_adsr2 ? P_DEC2 : P_DEC, when);
            if (n->num_children > 3)
                _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 3, nullptr, apply_value_func, is_adsr2 ? P_SUS2 : P_SUS, when);
            if (n->num_children > 4)
                _apply_values(dst, tmp_size, viz_time, left_hap, n->first_child + 4, nullptr, apply_value_func, is_adsr2 ? P_REL2 : P_REL, when);
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
            _make_haps(tmp0, tmp_size, viz_time, n->first_child, when, hash2_pcg(hapid, n->first_child));
        hap_span_t setsteps_haps =
            _make_haps(tmp1, tmp_size, viz_time, n->first_child + 1, when, hash2_pcg(hapid, n->first_child + 1));
        hap_span_t numsteps_haps =
            _make_haps(tmp2, tmp_size, viz_time, n->first_child + 2, when, hash2_pcg(hapid, n->first_child + 2));
        hap_span_t rot_haps = {};
        if (n->num_children > 3)
            rot_haps = _make_haps(tmp3, tmp_size, viz_time, n->first_child + 3, when, hash2_pcg(hapid, n->first_child + 3));
        if (rot_haps.empty())
            rot_haps = {};
        for (hap_t *numsteps_hap = numsteps_haps.s; numsteps_hap < numsteps_haps.e; numsteps_hap++) {
            int numsteps = (int)numsteps_hap->get_param(P_NUMBER, 0.f);
            if (numsteps < 1)
                continue;
            hap_time child_when = when * numsteps;
            int i = floor(child_when);
            hap_time t0 = i / (float)numsteps;
            hap_time t1 = (i + 1) / (float)numsteps;
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
    } // euclid
    break;

    } // switch
    if (appended && viz_time >= 0.f) {
        bfs_start_end[nodeidx].last_evaled_glfw_time = viz_time;
    }

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
