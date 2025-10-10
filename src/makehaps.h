// code for turning patterns (a tree of nodes) into haps (a flat list of events with times and values)

template <typename T> void arrsetlencap(T *&arr, int len, int cap) {
    assert(len <= cap);
    stbds_arrsetcap(arr, cap);
    stbds_arrsetlen(arr, len);
}

// convert the parsed node structure into an SoA bfs tree. also 'squeezes' single child lists.
pattern_t pattern_maker_t::get_pattern(const char *key) {
    pattern_t p = {.key = key, .nodes = nodes, .curvedata = curvedata, .root = root};
    int n_input = stbds_arrlen(nodes);
    arrsetlencap(p.bfs_start_end, 0, n_input);
    arrsetlencap(p.bfs_min_max_value, 0, n_input);
    arrsetlencap(p.bfs_nodes, 0, n_input);
    arrsetlencap(p.bfs_nodes_total_length, 0, n_input);
    int_pair_t q[n_input];
    int qhead = 1;
    q[0] = {root, -1};
    for (int i = 0; i < qhead; i++) {
        Node *n = nodes + q[i].k;
        bool has_one_child = n->first_child >= 0 && nodes[n->first_child].next_sib < 0;
        if (has_one_child && (n->type == N_CAT || (n->type == N_FASTCAT && n->total_length == 1.f) || n->type == N_PARALLEL)) {
            q[i--].k = n->first_child; // replace this node with its only child
            continue;
        }
        int bfs_parent = q[i].v;
        int my_bfs_idx = stbds_arrlen(p.bfs_nodes);
        // float previous_sibling_length = 0.f;
        if (bfs_parent >= 0 && p.bfs_nodes[bfs_parent].first_child < 0) {
            p.bfs_nodes[bfs_parent].first_child = my_bfs_idx;
        } /*else if (my_bfs_idx > 0) {
            previous_sibling_length = p.bfs_nodes_total_length[my_bfs_idx - 1];
        }*/
        int_pair_t start_end = {n->start, n->end};
        float_pair_t min_max_value = {n->min_value, n->max_value};
        bfs_node_t node = {n->type, n->value_type, (uint16_t)n->num_children, -1};
        // float cumulative_length = n->total_length + previous_sibling_length;
        stbds_arrpush(p.bfs_start_end, start_end);
        stbds_arrpush(p.bfs_min_max_value, min_max_value);
        stbds_arrpush(p.bfs_nodes, node);
        stbds_arrpush(p.bfs_nodes_total_length, n->total_length);
        for (int child = n->first_child; child >= 0; child = nodes[child].next_sib) {
            q[qhead++] = {child, my_bfs_idx};
        }
    }
    return p;
}

void pretty_print_nodes(const char *src, const char *srcend, pattern_t *p, int i = 0, int depth = 0, int numsiblings = 1) {
    if (i < 0)
        return;
    int c0 = p->bfs_start_end[i].k;
    int c1 = p->bfs_start_end[i].v;
    printf(COLOR_GREY "%d " COLOR_BLUE "%.*s" COLOR_BRIGHT_YELLOW "%.*s" COLOR_BLUE "%.*s" COLOR_RESET, i, c0, src, c1 - c0,
           src + c0, (int)(srcend - src - c1), src + c1);
    for (int j = 0; j < depth + 1; j++) {
        printf("  ");
    }
    bfs_node_t *n = p->bfs_nodes + i;
    switch (n->value_type) {
    case VT_SOUND: {
        Sound *sound = get_sound_by_index((int)p->bfs_min_max_value[i].v);
        printf("%d - %s - maxlen %g val %g-%g %s\n", i, node_type_names[n->type], p->bfs_nodes_total_length[i],
               p->bfs_min_max_value[i].k, p->bfs_min_max_value[i].v, sound ? sound->name : "");
        break;
    }
    case VT_NOTE: {
        printf("%d - %s - maxlen %g val %g-%g %s-%s\n", i, node_type_names[n->type], p->bfs_nodes_total_length[i],
               p->bfs_min_max_value[i].k, p->bfs_min_max_value[i].v, print_midinote((int)p->bfs_min_max_value[i].k),
               print_midinote((int)p->bfs_min_max_value[i].v));
        break;
    }
    default:
        printf("%d - %s - maxlen %g val %g-%g\n", i, node_type_names[n->type], p->bfs_nodes_total_length[i],
               p->bfs_min_max_value[i].k, p->bfs_min_max_value[i].v);
        break;
    }
    if (n->first_child >= 0) {
        pretty_print_nodes(src, srcend, p, n->first_child, depth + 1, n->num_children);
    }
    if (numsiblings > 1) {
        pretty_print_nodes(src, srcend, p, i + 1, depth, numsiblings - 1);
    }
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
    if (t == N_OP_REPLICATE || t == N_OP_ELONGATE)
        return bfs_min_max_value[nodeidx].v; // use max value as length
    return 1.f;
}

void pattern_t::_append_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid) {
    if (dst.s >= dst.e)
        return;
    hap_t *hap = dst.s++;
    hap->t0 = t0;
    hap->t1 = t1;
    hap->node = nodeidx;
    hap->hapid = hapid;
    bfs_node_t *n = bfs_nodes + nodeidx;
    int value_type = n->value_type;
    float_pair_t minmax = bfs_min_max_value[nodeidx];
    hap->valid_params = 1 << value_type;
    float v;
    if (minmax.k != minmax.v) { // randomize in range
        float t = (pcg_mix(pcg_next(hapid)) & 0xffffff) * (1.f / 0xffffff);
        v = lerp(minmax.k, minmax.v, t);
        if (n->value_type == VT_NOTE && minmax.v - minmax.k >= 1.f) // if it's a range of at least two semitone,
                                                                    // quantize to nearest semitone
            v = roundf(v);
    } else
        v = minmax.v;
    hap->params[value_type] = v;
}

hap_span_t pattern_t::_make_haps(hap_span_t &dst, hap_span_t tmp, int nodeidx, hap_time a, hap_time b, int hapid) {
    hap_span_t rv = {dst.s, dst.s};
    if (nodeidx < 0 || a > b)
        return rv;
    bfs_node_t *n = bfs_nodes + nodeidx;
    float speed_scale = 1.f;
    float total_length = bfs_nodes_total_length[nodeidx];
    switch (n->type) {
    case N_OP_TIMES:
    case N_OP_DIVIDE: {
        hap_span_t old_dst = dst;
        dst = tmp;
        dst=old_dst;
        break; }
    case N_LEAF:
        for (int i = floor(a); i < b; ++i) {            
            _append_hap(dst, nodeidx, i, i + 1, hash2_pcg(hapid, i));
        }
        break;
    case N_FASTCAT:
        speed_scale = total_length;
        // fall thru
    case N_CAT: {
        if (total_length <= 0.f || n->first_child < 0)
            break;
        float child_a = a * speed_scale, child_b = b * speed_scale;
        int loopidx = floor(child_a / total_length);
        float from = loopidx * total_length;
        int childidx = 0;
        while (from < child_b) {
            int childnode = n->first_child + childidx;
            float child_length = get_length(childnode);
            float to = from + child_length;
            if (to <= from)
                break;
            if (to > child_a) {
                // we shift time so that the child feels like it's just looping on its own
                hap_time child_from = loopidx * child_length;
                hap_time child_to = (loopidx + 1) * child_length;
                float tofs = from - child_from;
                // we pass down the query range scaled & shifted into child local time,
                // but we also clip it to the extent of this part of the cat (from-to)
                // in that way, large query ranges dont return multiple copies of the children
                // but also, small query ranges propagate down the callstack to keep things fast.
                hap_span_t newhaps =
                    _make_haps(dst, tmp, childnode, max(child_a, from) - tofs, min(child_b, to) - tofs, hash2_pcg(hapid, childnode));
                for (hap_t *src_hap = newhaps.s; src_hap < newhaps.e; src_hap++) {
                    src_hap->t0 = (src_hap->t0 + tofs) / speed_scale;
                    src_hap->t1 = (src_hap->t1 + tofs) / speed_scale;
                    if (src_hap->t0 >= b || src_hap->t1 <= a) { // filter out haps that are outside the query range
                        src_hap->valid_params = 0;
                    }
                }
            }
            if (++childidx == n->num_children) {
                childidx = 0;
                loopidx++;
            }
            from = to;
        }
        break;
    }
    }
    rv.e = dst.s;
    return rv;
} // make_haps

void pretty_print_haps(hap_span_t haps, hap_time from, hap_time to) {
    for (hap_t *hap = haps.s; hap < haps.e; hap++) {
        // hap onset must overlap the query range
        if (hap->t0 < from || hap->t0 >= to)
            continue;
        printf(COLOR_GREY "%08x(%d) " COLOR_CYAN "%5.2f" COLOR_BLUE " -> %5.2f " COLOR_RESET, hap->hapid, hap->node,
               hap->t0 / double(hap_cycle_time), hap->t1 / double(hap_cycle_time));
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

/*
static inline void init_hap_in_place(hap_t *dst, hap_time t0, hap_time t1, int nodeidx, int hapid, int value_type, float value) {
    dst->t0 = t0;
    dst->t1 = t1;
    dst->node = nodeidx;
    dst->valid_params = 1 << value_type;
    dst->hapid = hapid;
    dst->params[value_type] = value;
}

static inline int cmp_haps_by_t0(const hap_t *a, const hap_t *b) { return a->t0 < b->t0 ? -1 : a->t0 > b->t0 ? 1 : 0; }

static inline void sort_haps_by_t0(hap_span_t haps) {
    qsort(haps.s, haps.e - haps.s, sizeof(hap_t), (int (*)(const void *, const void *))cmp_haps_by_t0);
}

hap_span_t pattern_t::_append_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid) {
    if (dst.s >= dst.e)
        return {};
    Node *n = nodes + nodeidx;
    int value_type = n->value_type;
    if (value_type <= VT_NONE)
        return {};
    bool is_rest = value_type == VT_SOUND && n->max_value < 2;
    if (is_rest)
        return {};
    float v = n->max_value;
    if (n->min_value != n->max_value) { // randomize in range
        float t = (pcg_mix(pcg_next(hapid)) & 0xffffff) * (1.f / 0xffffff);
        v = lerp(n->min_value, n->max_value, t);
        if (value_type == VT_NOTE && n->max_value - n->min_value >= 1.f) // if it's a range of at least two semitone, quantize to
                                                                         // nearest semitone
            v = roundf(v);
    }
    init_hap_in_place(dst.s++, t0, t1, nodeidx, hapid, value_type, v);
    return {dst.s - 1, dst.s};
}

// 'global' time is the output timeline.
// 'local' time is the time from the perspective of this particular node.
// tofs sets the zero point of local time in global time.
// tscale is the speedup factor of global vs local. so tscale<1 sounds faster.

static inline hap_time global_to_local(hap_time t, fraction_t tscale, hap_time tofs) {
    return scale_time(t - tofs, tscale.inverse());
}
static inline hap_time local_to_global(hap_time t, fraction_t tscale, hap_time tofs) { return scale_time(t, tscale) + tofs; }

// consumes dst; returns a new span of added haps, either with dst or empty.
hap_span_t pattern_t::_make_haps(hap_span_t &dst, hap_span_t &tmp, int nodeidx, hap_time global_t0, hap_time global_t1,
                                 fraction_t tscale, hap_time tofs, int flags, int hapid) {
    if (nodeidx < 0 || global_t0 > global_t1)
        return {};
    Node *n = nodes + nodeidx;
    if (n->type == N_LEAF && (flags & FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES)) {
        int loop_idx = haptime2cycleidx(global_to_local(global_t0, tscale, tofs));
        return _append_hap(dst, nodeidx, global_t0, global_t1, hash2_pcg(hapid, loop_idx));
    }
    hapid = hash2_pcg(hapid, nodeidx); // go deeper in the tree...
    hap_span_t rv = {dst.s, dst.s};
    Node *first_child = (n->first_child < 0) ? NULL : nodes + n->first_child;
    switch (n->type) {
    case N_OP_EUCLID: {
        if (!first_child)
            return {};
        int setsteps_nodeidx = first_child->next_sib;
        if (setsteps_nodeidx < 0)
            return {};
        int numsteps_nodeidx = nodes[setsteps_nodeidx].next_sib;
        if (numsteps_nodeidx < 0)
            return {};
        int rot_nodeidx = nodes[numsteps_nodeidx].next_sib;
        hap_span_t setsteps_haps = _make_haps(tmp, tmp, setsteps_nodeidx, global_t0, global_t1, tscale, tofs,
                                              FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 1);
        hap_span_t numsteps_haps = _make_haps(tmp, tmp, numsteps_nodeidx, global_t0, global_t1, tscale, tofs,
                                              FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 2);
        hap_span_t rot_haps = _make_haps(tmp, tmp, rot_nodeidx, global_t0, global_t1, tscale, tofs,
                                         FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 3);
        sort_haps_by_t0(setsteps_haps);
        sort_haps_by_t0(numsteps_haps);
        sort_haps_by_t0(rot_haps);
        hap_time from = global_t0;
        while (from < global_t1) {
            while (setsteps_haps.hasatleast(2) && setsteps_haps.s[1].t0 <= from)
                setsteps_haps.s++;
            while (numsteps_haps.hasatleast(2) && numsteps_haps.s[1].t0 <= from)
                numsteps_haps.s++;
            while (rot_haps.hasatleast(2) && rot_haps.s[1].t0 <= from)
                rot_haps.s++;
            hap_time to = global_t1;
            to = min(to, setsteps_haps.hasatleast(2) ? setsteps_haps.s[1].t0 : global_t1);
            to = min(to, numsteps_haps.hasatleast(2) ? numsteps_haps.s[1].t0 : global_t1);
            to = min(to, rot_haps.hasatleast(2) ? rot_haps.s[1].t0 : global_t1);
            if (to <= from)
                break;
            int setsteps = setsteps_haps.empty() ? 0 : nodes[setsteps_haps.s[0].node].max_value;
            int numsteps = numsteps_haps.empty() ? 1 : nodes[numsteps_haps.s[0].node].max_value;
            int rot = rot_haps.empty() ? 0 : nodes[rot_haps.s[0].node].max_value;
            if (numsteps <= 0)
                break;
            fraction_t child_tscale = tscale / numsteps;
            hap_time child_t0 = global_to_local(from, child_tscale, tofs);
            hap_time child_t1 = global_to_local(to, child_tscale, tofs);
            int stepidx = floor2cycle(child_t0);
            while (child_t0 < child_t1) {
                if (euclid_rhythm(haptime2cycleidx(stepidx), setsteps, numsteps, rot)) {
                    hap_time step_from = local_to_global(stepidx, child_tscale, tofs);
                    hap_time step_to = local_to_global(stepidx + hap_cycle_time, child_tscale, tofs);
                    _make_haps(dst, tmp, n->first_child, max(global_t0, step_from), min(global_t1, step_to), child_tscale, tofs,
                               flags, hapid + haptime2cycleidx(stepidx));
                }
                child_t0 = (stepidx += hap_cycle_time);
            }
            from = to;
        }
        break;
    }
    case N_OP_DEGRADE:
    case N_OP_IDX:
    case N_OP_REPLICATE:
    case N_OP_ELONGATE:
    case N_OP_DIVIDE:
    case N_OP_TIMES: {
        float right_value = 0.5f;
        hap_span_t right_haps = {};
        int num_right_haps = 1;
        if (first_child->next_sib >= 0) {
            right_haps = _make_haps(tmp, tmp, first_child->next_sib, global_t0, global_t1, tscale, tofs,
                                    FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 1);
            num_right_haps = right_haps.e - right_haps.s;
        }
        int target_type = (n->type == N_OP_IDX) ? P_NUMBER : -1;
        for (int i = 0; i < num_right_haps; i++) {
            if (!right_haps.empty()) {
                hap_t *right_hap = right_haps.s + i;
                if (!(right_hap->valid_params & (1 << P_NUMBER)))
                    continue; // TODO  does it ever make sense for the right side to have non-number data?
                right_value = right_hap->params[P_NUMBER];
            }
            fraction_t child_tscale;
            if (n->type == N_OP_TIMES) {
                if (right_value <= 0.f)
                    continue;
                child_tscale = tscale / right_value;
            } else if (n->type == N_OP_DIVIDE || n->type == N_OP_ELONGATE) {
                if (right_value <= 0.f)
                    continue;
                child_tscale = tscale * (right_value ? right_value : 1.f);
            } else
                child_tscale = tscale;
            hap_time child_t0 = !right_haps.empty() ? right_haps.s[i].t0 : global_t0;
            hap_time child_t1 = !right_haps.empty() ? right_haps.s[i].t1 : global_t1;
            child_t0 = max(global_t0, child_t0);
            child_t1 = min(global_t1, child_t1);
            hap_span_t child_haps = _make_haps(dst, tmp, n->first_child, child_t0, child_t1, child_tscale, tofs, flags, hapid + i);
            if (n->type == N_OP_DEGRADE) {
                // blank out degraded haps
                if (right_value >= 1.f)
                    dst.e = rv.e; // delete all outputs!
                else if (right_value > 0.f)
                    for (hap_t *hap = child_haps.s; hap < child_haps.e; hap++) {
                        float t = (pcg_mix(pcg_next(23124 - hap->hapid)) & 0xffffff) * (1.f / 0xffffff);
                        if (right_value >= t) {
                            hap->valid_params = 0; // mark it as invalid basically
                        }
                    }
            } else if (target_type >= 0) {
                for (hap_t *hap = child_haps.s; hap < child_haps.e; hap++) {
                    hap->params[target_type] = right_value;
                    hap->valid_params |= 1 << target_type;
                    // todo update hap id?
                }
            }
        }
        break;
    }
    case N_RANDOM: {
        int num_children = 0;
        for (int i = n->first_child; i >= 0; i = nodes[i].next_sib)
            ++num_children;
        if (!num_children)
            return {};
        int kids[num_children];
        num_children = 0;
        for (int i = n->first_child; i >= 0; i = nodes[i].next_sib)
            kids[num_children++] = i;
        hap_time t0 = global_to_local(global_t0, tscale, tofs);
        hap_time t1 = global_to_local(global_t1, tscale, tofs);
        int stepidx = floor2cycle(t0);
        hap_time from = t0;
        while (from < t1) {
            int kid = kids[hash2_pcg(hapid, haptime2cycleidx(stepidx)) % num_children];
            hap_time child_t0 = max(global_t0, local_to_global(stepidx, tscale, tofs));
            hap_time child_t1 = min(global_t1, local_to_global(stepidx + hap_cycle_time, tscale, tofs));
            _make_haps(dst, tmp, kid, child_t0, child_t1, tscale, tofs, flags, hapid + kid);
            from = (stepidx += hap_cycle_time);
        }
        break;
    }
    case N_FASTCAT:
        if (n->total_length <= 0.f)
            break;
        tscale = tscale / n->total_length;
    case N_LEAF:
    case N_CAT: {
        hap_time total_length = hap_time(n->total_length * hap_cycle_time);
        hap_time t0 = global_to_local(global_t0, tscale, tofs);
        hap_time t1 = global_to_local(global_t1, tscale, tofs);
        int loop_index = total_length > 0 ? t0 / total_length : 0;
        int child = n->first_child;
        hap_time from = loop_index * total_length;
        while (from < t1) {
            if (child >= 0) {
                Node *child_n = nodes + child;
                hap_time child_length = (hap_time)(get_length(this, child) * hap_cycle_time);
                if (child_length <= 0)
                    child_length = hap_cycle_time;
                hap_time to = from + child_length;
                if (to > t0 && from < t1) {
                    // this child overlaps the query range
                    hap_time child_from = loop_index * child_length;
                    hap_time child_to = (loop_index + 1) * child_length;
                    _make_haps(dst, tmp, child, max(global_t0, local_to_global(from, tscale, tofs)),
                               min(global_t1, local_to_global(to, tscale, tofs)), tscale,
                               tofs + scale_time(from - child_from, tscale), flags, hash2_pcg(hash2_pcg(hapid, loop_index), child));
                }
                from = to;
                child = child_n->next_sib;
                if (child < 0) {
                    child = n->first_child;
                    loop_index++;
                }
            } else { // leaf
                hap_time to = from + hap_cycle_time;
                if (to > t0 && from < t1) {
                    if (nodeidx >= 0 && nodes[nodeidx].value_type > VT_NONE) {
                        // we do NOT clip this one, so that we know when the notedown was!
                        // but we do skip it entirely if its outside the query range,
                        // and we check with an epsilon in *global* time.
                        hap_time t0g = local_to_global(from, tscale, tofs);
                        hap_time t1g = local_to_global(to, tscale, tofs);
                        if (t1g > global_t0 + epsilon && t0g < global_t1 - epsilon)
                            _append_hap(dst, nodeidx, t0g, t1g, hash2_pcg(hapid, loop_index));
                    }
                }
                from = to;
                loop_index++;
            }
        }
        break;
    }
    case N_POLY:
    case N_PARALLEL: {
        int child = n->first_child;
        float speed_scale = 1.f;
        if (n->type == N_POLY) {
            speed_scale = (n->max_value > 0) ? n->max_value : (child >= 0) ? nodes[child].total_length : 1.;
        }
        int childidx = 0;
        if (speed_scale < 0.f)
            break;
        tscale = tscale / speed_scale;
        while (child >= 0) {
            _make_haps(dst, tmp, child, global_t0, global_t1, tscale, tofs, flags, hapid + childidx);
            child = nodes[child].next_sib;
            ++childidx;
        }
        break;
    }
    }
    rv.e = dst.s;
    return rv;
}
    */
