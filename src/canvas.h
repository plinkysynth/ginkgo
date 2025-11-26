GLuint strokevao = 0, strokevbo = 0;
int stroke_capacity = 1024;
int stroke_dirty_from = 0;

bool load_canvas(EditorState *E) {
    sj_Reader r = read_json_file(E->fname);
    for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
        if (iter_key_is(&outer, "patterns")) {
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                const char *patname = temp_cstring_from_span(inner.key.start, inner.key.end);
                pattern_t *p = get_pattern(patname);
                if (!p) continue;
                for (sj_iter_t inner2 = iter_start(inner.r, &inner.val); iter_next(&inner2);) {
                    if (iter_key_is(&inner2, "x")) {
                        p->x = iter_val_as_float(&inner2, p->x);
                    } else if (iter_key_is(&inner2, "y")) {
                        p->y = iter_val_as_float(&inner2, p->y);
                    }
                }
            } // pattern loop
        } // patterns object
        if (iter_key_is(&outer, "strokes")) {
            stroke_t *strokes = NULL;
            for (sj_iter_t strokearr = iter_start(outer.r, &outer.val); iter_next(&strokearr);) {
                sj_iter_t strokeval = iter_start(strokearr.r, &strokearr.val);
                stroke_t s={};
                iter_next(&strokeval);
                s.x = iter_val_as_float(&strokeval, 0.f);
                iter_next(&strokeval);
                s.y = iter_val_as_float(&strokeval, 0.f);
                iter_next(&strokeval);
                s.inner_rad = iter_val_as_float(&strokeval, 0.f);
                iter_next(&strokeval);
                s.outer_rad = iter_val_as_float(&strokeval, 0.f);
                iter_next(&strokeval);
                s.col = iter_val_as_uint(&strokeval, 0xffffffff);
                iter_next(&strokeval);
                stbds_arrpush(strokes, s);
            }
            stbds_arrpush(E->strokes, strokes);
            E->strokes_idx = stbds_arrlen(E->strokes)-1;
            stroke_dirty_from=0;            
        }
    } // outer loop
    free_json(&r);
    return true;
}

bool save_canvas(EditorState *E) {
    FILE *f = fopen(E->fname, "wb");
    if (!f)
        return false;
    json_printer_t jp = {f};
    json_start_object(&jp, NULL);
    json_start_object(&jp, "patterns");
    for (int i = 0; i < stbds_shlen(G->patterns_map); i++) {
        pattern_t *p = &G->patterns_map[i];
        if (!p->colbitmask)
            continue;
        json_start_object(&jp, p->key);
        json_print(&jp, "x", p->x);
        json_print(&jp, "y", p->y);
        json_end_object(&jp);
    }
    json_end_object(&jp); // patterns

    json_start_array(&jp, "strokes");
    stroke_t *strokes = E->strokes ? E->strokes[E->strokes_idx] : NULL;
    for (int i = 0; i < stbds_arrlen(strokes); ++i) {
        json_start_array(&jp, "");
        json_print(&jp, "x", strokes[i].x);
        json_print(&jp, "y", strokes[i].y);
        json_print(&jp, "inner_rad", strokes[i].inner_rad);
        json_print(&jp, "outer_rad", strokes[i].outer_rad);
        json_print(&jp, "col", strokes[i].col);
        json_end_array(&jp);
    }
    json_end_array(&jp); // strokes
    json_end_object(&jp); // anonymous {}
    json_end_file(&jp);
    return true;
}

void init_stroke_buffer(void) {
    static const uint32_t attrib_sizes[2] = {4, 4};
    static const uint32_t attrib_types[2] = {GL_FLOAT, GL_UNSIGNED_BYTE};
    static const uint32_t attrib_offsets[2] = {offsetof(stroke_t, x), offsetof(stroke_t, col)};
    init_dynamic_buffer(1, &strokevao, &strokevbo, 2, attrib_sizes, attrib_types, attrib_offsets, stroke_capacity,
                        sizeof(stroke_t));
    check_gl("init stroke buffer post");
}

void canvas_undo(EditorState *E) {
    if (E->strokes_idx > 0) {
        E->strokes_idx--;
        stroke_dirty_from = 0;
    }
}

void canvas_redo(EditorState *E) {
    if (E->strokes_idx < stbds_arrlen(E->strokes) - 1) {
        E->strokes_idx++;
        stroke_dirty_from = 0;
    }
}

void update_pattern_over_color(pattern_t *p, EditorState *canvasE) {
    if (!canvasE || !p || !canvasE->strokes)
        return;
    if (!p->colbitmask)
        return;
    assert(canvasE->editor_type == TAB_CANVAS);
    stroke_t *strokes = canvasE->strokes[canvasE->strokes_idx];
    float4 over_color = {0.f};
    // TODO : acceleration structure for the strokes.
    for (int i = 0; i < stbds_arrlen(strokes); i++) {
        stroke_t *s = &strokes[i];
        float dx = s->x - p->x;
        float dy = s->y - p->y;
        float dsq = dx * dx + dy * dy;
        float inner_rad = s->inner_rad;
        float outer_rad = s->outer_rad + 10.f; // always add a bit of softness
        if (dsq > outer_rad * outer_rad)
            continue;
        float alpha;
        float d = sqrtf(dsq);
        if (d <= s->inner_rad)
            alpha = 1.f;
        else
            alpha = 1.f - (d - inner_rad) / (outer_rad - inner_rad);
        if (alpha < 0.f)
            continue;
        alpha *= (s->col >> 24) / 255.f;
        over_color *= (1.f - alpha);
        over_color.x += ((s->col >> 0) & 0xff) * (alpha / 255.f);
        over_color.y += ((s->col >> 8) & 0xff) * (alpha / 255.f);
        over_color.z += ((s->col >> 16) & 0xff) * (alpha / 255.f);
        over_color.w += alpha;
    }
    p->over_color = over_color;
}

void update_all_pattern_over_colors(pattern_t *patterns, EditorState *E) {
    for (int i = 0; i < stbds_shlen(patterns); i++) {
        pattern_t *p = &patterns[i];
        if (p->x == 0.f && p->y == 0.f) {
            // uninited position
            p->x = i * 60.f + 30.f;
            p->y = 1080.f / 4.f;
        }
        update_pattern_over_color(p, E);
    }
}

void draw_text(float x, float y, const char *text, float size, uint32_t col = 0xffffffff) {
    for (const char *c = text; *c; c++) {
        add_line(x, y, x + 0.001f, y, col, size, 0.f, *c);
        x += size * 0.5f;
    }
}

void draw_arrow(float px, float py, float tpx, float tpy, float lw, float arrowhead) {
    float dx = (tpx - px);
    float dy = (tpy - py);
    float d = sqrtf(dx * dx + dy * dy);
    if (d) {
        dx *= arrowhead / d;
        dy *= arrowhead / d;
        tpx -= dx * ((20.f + lw) / 15.f);
        tpy -= dy * ((20.f + lw) / 15.f);
        add_line(px, py, tpx, tpy, 0xffffffff, lw);
        add_line(tpx, tpy, tpx - dx - dy, tpy - dy + dx, 0xffffffff, lw);
        add_line(tpx, tpy, tpx - dx + dy, tpy - dy - dx, 0xffffffff, lw);
    }
}

void draw_canvas(EditorState *E) {
    update_zoom_and_center(E);
    const static uint32_t cols[] = {0xffffffff, 0xff0000ee, 0xff00d0fc, 0xff00ee00, 0xfffcd000,
                                    0xffee0000, 0xffd000fc, 0xff000000, 0};
    if (E->strokes == NULL) {
        stroke_t *strokes = NULL;
        stbds_arrpush(E->strokes, strokes);
        E->strokes_idx = 0;
    }
    static double last_time = G->iTime;
    double dt = G->iTime - last_time;
    last_time = G->iTime;
    static double curx = 0, cury = 0;
    static bool was_drawing = false;
    static int drag_pattern_idx = -1;

    double x = (G->mx - E->centerx_sm) / E->zoom_sm;
    double y = (G->my - E->centery_sm) / E->zoom_sm;
    RawPen *pen = mac_pen_get();
    bool mouse_down = G->mb || (pen && pen->down && pen->pressure > 1.f / 255.f);
    int pressure = 255;
    if (pen) {
        // pen pos seems to lag glfw by a frame, so we trust the glfw mouse pos for now.
        // x = (pen->x * G->fbw - E->centerx_sm) / E->zoom_sm;
        // y = (pen->y * G->fbh - E->centery_sm) / E->zoom_sm;
        pressure = clamp((int)(sqrtf(pen->pressure) * 255.f), 1, 255);
        // add_line(pen->x * G->fbw, pen->y * G->fbh, pen->x * G->fbw, pen->y * G->fbh, 0xffff00ff, 500.f * pen->pressure);
    }

    if (!E->cur_inner_rad)
        E->cur_inner_rad = 30.f;
    if (!E->cur_outer_rad)
        E->cur_outer_rad = 40.f;
    if (E->cur_inner_rad + 1.f > E->cur_outer_rad)
        E->cur_outer_rad = E->cur_inner_rad + 1.f;
    float inner_rad = E->cur_inner_rad;
    float outer_rad = E->cur_outer_rad;
    int hover_idx = (G->my > G->fbh - 60.f) ? (G->mx - 30.f) / 80.f + 0.5f : -1;
    if (hover_idx >= 9 || was_drawing)
        hover_idx = -1;

    float extrarad = 4.f / E->zoom_sm;
    int hover_pattern_idx = drag_pattern_idx;
    float lw = 8.f * E->zoom_sm;
    float arrowdx = 50.f * E->zoom_sm;
    float arrowhead = 15.f * E->zoom_sm;
    for (int i = 0; i < stbds_shlen(G->patterns_map); i++) {
        pattern_t *p = &G->patterns_map[i];
        int cbm = p->colbitmask;
        float px = p->x * E->zoom_sm + E->centerx_sm;
        float py = p->y * E->zoom_sm + E->centery_sm;
        if (cbm & (1 << 8)) {
            // x
            add_line(px - arrowdx, py, px + arrowdx, py, 0xffffffff, lw * 0.25f);
            add_line(px + arrowdx, py, px + arrowdx - arrowhead, py + arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px + arrowdx, py, px + arrowdx - arrowhead, py - arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px - arrowdx, py, px - arrowdx + arrowhead, py + arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px - arrowdx, py, px - arrowdx + arrowhead, py - arrowhead, 0xffffffff, lw * 0.25f);
        }
        if (cbm & (1 << 9)) {
            // y
            add_line(px, py - arrowdx, px, py + arrowdx, 0xffffffff, lw * 0.25f);
            add_line(px, py + arrowdx, px + arrowhead, py + arrowdx - arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px, py + arrowdx, px - arrowhead, py + arrowdx - arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px, py - arrowdx, px + arrowhead, py - arrowdx + arrowhead, 0xffffffff, lw * 0.25f);
            add_line(px, py - arrowdx, px - arrowhead, py - arrowdx + arrowhead, 0xffffffff, lw * 0.25f);
        }
        if (cbm & (1 << 25)) {
            // blendnear
            for (int j = 0; j < stbds_arrlen(p->bfs_nodes); j++) {
                if (p->bfs_nodes[j].type == N_BLENDNEAR) {
                    float weights[p->bfs_nodes[j].num_children];
                    float tot_weight = p->compute_blendnear_weights(&p->bfs_nodes[j], weights);
                    int first_child = p->bfs_nodes[j].first_child;
                    for (int ch = 0; ch < p->bfs_nodes[j].num_children; ch++) {
                        int pidx = (int)p->bfs_min_max_value[first_child + ch].mx;
                        if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) {
                            pattern_t *target_pat = &G->patterns_map[pidx];
                            float tpx = target_pat->x * E->zoom_sm + E->centerx_sm;
                            float tpy = target_pat->y * E->zoom_sm + E->centery_sm;
                            draw_arrow(px, py, tpx, tpy, lw * weights[ch] / tot_weight, arrowhead);
                            tpx += 30.f * E->zoom_sm;
                            uint32_t col = 0xff808080;
                            if (target_pat->picked > 0.5f) {
                                col=0xffffffff;
                            }
                            draw_text(tpx, tpy, target_pat->key, 30.f * E->zoom_sm,  col);
                        } // target pattern
                    } // child loop
                } // blendnear node
            } // node loop
        }

        if (cbm & (1 << 24)) {
            // near
            for (int j = 0; j < stbds_arrlen(p->bfs_nodes); j++) {
                if (p->bfs_nodes[j].type == N_NEAR) {
                    int pidx = (int)p->bfs_min_max_value[j].mx;
                    if (pidx >= 0 && pidx < stbds_shlen(G->patterns_map)) {
                        pattern_t *target_pat = &G->patterns_map[pidx];
                        float tpx = target_pat->x * E->zoom_sm + E->centerx_sm;
                        float tpy = target_pat->y * E->zoom_sm + E->centery_sm;
                        float nearness = p->get_near_output(target_pat);
                        if (nearness > 0.f) {
                            draw_arrow(px, py, tpx, tpy, lw * nearness, arrowhead);
                            tpx += 30.f * E->zoom_sm;
                            draw_text(tpx, tpy, target_pat->key, 30.f * E->zoom_sm, 0xff808080);
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i < stbds_shlen(G->patterns_map); i++) {
        pattern_t *p = &G->patterns_map[i];
        if (!p->colbitmask)
            continue;
        float px = p->x * E->zoom_sm + E->centerx_sm;
        float py = p->y * E->zoom_sm + E->centery_sm;
        int cbm = p->colbitmask & 0xff;
        // draw a color ring per color in the bitmask
        float dmx = x - p->x, dmy = y - p->y;
        float dm = sqrtf(dmx * dmx + dmy * dmy);
        if (dm < 20.f + extrarad && hover_pattern_idx == -1) {
            uint32_t overcol = (int(p->over_color.x * 255.f) << 0) | (int(p->over_color.y * 255.f) << 8) |
                               (int(p->over_color.z * 255.f) << 16) | (int(p->over_color.w * 255.f) << 24);
            add_line(px, py, px, py, overcol, (20.f + 8.f) * E->zoom_sm * 2.f);
            add_line(px, py, px, py, 0xffffffff, (20.f + 4.f) * E->zoom_sm * 2.f);
            hover_pattern_idx = i;
        }
        add_line(px, py, px, py, 0xff000000, (21.f) * E->zoom_sm * 2.f); // outline
        if (!cbm) {
            // no colors, draw a gray circle
            uint32_t grey_col = 0xff808080;
            if (!(p->colbitmask & 0x0fffffff)) { // bit 28-31 are the arrow head targets.
                // its only the *target* of an arrow head, so make it unfilled.
                add_line(px, py, px, py, grey_col, (20.f) * E->zoom_sm * 2.f, -2.f); 
            } else {
                add_line(px, py, px, py, grey_col, (20.f) * E->zoom_sm * 2.f);
            }
        } else {
            int numcbm = popcount(cbm);
            for (int j = 0; j < numcbm; j++) {
                int ci = ctz(cbm);
                float rad = sqrtf((numcbm - j) / float(numcbm)) * 20.f * E->zoom_sm;
                add_line(px, py, px, py, cols[ci], rad * 2.f);
                cbm &= ~(1 << ci);
            }
        }
    }

    // draw bounding box.
    add_line(0.f * E->zoom_sm + E->centerx_sm, 0.f * E->zoom_sm + E->centery_sm, 1920.f * E->zoom_sm + E->centerx_sm,
             0.f * E->zoom_sm + E->centery_sm, 0xffffffff, 1.f);
    add_line(1920.f * E->zoom_sm + E->centerx_sm, 0.f * E->zoom_sm + E->centery_sm, 1920.f * E->zoom_sm + E->centerx_sm,
             1080.f * E->zoom_sm + E->centery_sm, 0xffffffff, 1.f);
    add_line(1920.f * E->zoom_sm + E->centerx_sm, 1080.f * E->zoom_sm + E->centery_sm, 0.f * E->zoom_sm + E->centerx_sm,
             1080.f * E->zoom_sm + E->centery_sm, 0xffffffff, 1.f);
    add_line(0.f * E->zoom_sm + E->centerx_sm, 1080.f * E->zoom_sm + E->centery_sm, 0.f * E->zoom_sm + E->centerx_sm,
             0.f * E->zoom_sm + E->centery_sm, 0xffffffff, 1.f);

    if (hover_pattern_idx != -1) {
        pattern_t *p = &G->patterns_map[hover_pattern_idx];
        float tx = (p->x + 30.f) * E->zoom_sm + E->centerx_sm;
        float ty = p->y * E->zoom_sm + E->centery_sm;
        draw_text(tx, ty, p->key, 30.f * E->zoom_sm);
    }

    if (hover_idx == -1 && hover_pattern_idx == -1) {
        add_line(G->mx, G->my, G->mx, G->my, 0xffffffff, inner_rad * 2.f, -1.f); // brush mouse cursor
        add_line(G->mx, G->my, G->mx, G->my, 0x80808080, outer_rad * 2.f, -1.f); // brush mouse cursor
    }
    float pressure_to_size = max(0.25f, pressure / 255.f);
    inner_rad *= pressure_to_size;
    outer_rad *= pressure_to_size;

    static float y_offsets[9] = {};
    for (int i = 0; i < countof(cols); i++) {
        int c = cols[i];
        float x = 30.f + i * 80.f;
        float y = G->fbh;
        float y_offset_target = (i == hover_idx) ? -15.f : 0.f;
        float radius = (i == E->cur_col_idx) ? 40.f : 30.f;
        y_offsets[i] += (y_offset_target - y_offsets[i]) * 0.1;
        y += y_offsets[i];
        add_line(x, y, x, y, c, radius * 2.f);
        if (i > 6) {
            add_line(x, y, x, y, 0xffffffff, radius * 2.f, -1.f);
        }
        if (i == 8) {
            const float rsq = radius * 0.7071067812f - 1.f;
            add_line(x - rsq, y - rsq, x + rsq, y + rsq, 0xffffffff, 2.f);
            add_line(x - rsq, y + rsq, x + rsq, y - rsq, 0xffffffff, 2.f);
        }
        if (i == E->cur_col_idx) {
            add_line(x, y, x, y, 0xffffffff, radius * 2.f, -2.f);
        }
    }

    if (!mouse_down) {
        curx = x;
        cury = y;
        was_drawing = false;
        drag_pattern_idx = -1;
    } else {
        if (hover_idx != -1) {
            E->cur_col_idx = hover_idx;
        } else if (hover_pattern_idx != -1 && drag_pattern_idx == -1) {
            drag_pattern_idx = hover_pattern_idx;
        } else if (drag_pattern_idx != -1 && drag_pattern_idx < stbds_shlen(G->patterns_map)) {
            pattern_t *p = &G->patterns_map[drag_pattern_idx];
            p->x = x;
            p->y = y;
            update_pattern_over_color(p, E);
        } else {
            if (!was_drawing) {
                // append a copy of the current strokes to the undo history
                while (stbds_arrlen(E->strokes) > E->strokes_idx + 1) {
                    stbds_arrfree(E->strokes[stbds_arrlen(E->strokes) - 1]);
                    stbds_arrdeln(E->strokes, stbds_arrlen(E->strokes) - 1, 1);
                }
                stroke_t *stroke_copy = NULL;
                stbds_arrsetlen(stroke_copy, stbds_arrlen(E->strokes[E->strokes_idx]));
                memcpy(stroke_copy, E->strokes[E->strokes_idx], stbds_arrlen(E->strokes[E->strokes_idx]) * sizeof(stroke_t));
                stbds_arrpush(E->strokes, stroke_copy);
                if (stbds_arrlen(E->strokes) > 100) {
                    stbds_arrfree(E->strokes[0]);
                    stbds_arrdeln(E->strokes, 0, 1);
                }
                E->strokes_idx = stbds_arrlen(E->strokes) - 1;
                printf("strokes_idx = %d\n", E->strokes_idx);
            }
            float dx = x - curx;
            float dy = y - cury;
            float stamp_every = max(2.f, max(outer_rad - inner_rad, inner_rad * 0.25f));

            float dist = sqrtf(square(dx) + square(dy)) / stamp_every * E->zoom_sm;
            if (dist > 20.f) {
                int i = 1;
            }
            if (!was_drawing)
                dist = max(dist, 1.f);
            was_drawing = true;
            if (dist >= 1.f) {
                dx /= dist;
                dy /= dist;
                while (dist >= 1.f) {
                    dist -= 1.f;
                    curx += dx;
                    cury += dy;
                    if (E->cur_col_idx < 8) {
                        uint32_t cc = cols[E->cur_col_idx] & 0xffffff;
                        cc |= (pressure << 24);
                        stroke_t s = {(float)curx, (float)cury, inner_rad / E->zoom_sm, outer_rad / E->zoom_sm, cc};
                        stbds_arrpush(E->strokes[E->strokes_idx], s);
                        if ((stbds_arrlen(E->strokes[E->strokes_idx]) % 100) == 0) {
                            printf("num strokes = %d\n", (int)stbds_arrlen(E->strokes[E->strokes_idx]));
                        }
                    } else {
                        // Find the closest entry in strokes and check for overlap.
                        for (int si = stbds_arrlen(E->strokes[E->strokes_idx]); si-- > 0;) {
                            const stroke_t *splat = &E->strokes[E->strokes_idx][si];
                            float dxs = (float)curx - splat->x;
                            float dys = (float)cury - splat->y;
                            float dist_sq = dxs * dxs + dys * dys;
                            float overlap_radius = (inner_rad / E->zoom_sm + splat->inner_rad) * 0.5f + 1.f / E->zoom_sm;
                            if (dist_sq < overlap_radius * overlap_radius) {
                                stbds_arrdeln(E->strokes[E->strokes_idx], si, 1);
                                stroke_dirty_from = si;
                                break;
                            }
                        }
                    } // deletion
                } // splat loop
            } // valid splat count
        } // clicking in canvas
    } // mouse button down

    // add_line(500.f, 500.f, 500.1f, 500.f, 0xffffffff, 500.f, 75.f, 'A');
    // add_line(750.f, 500.f, 750.1f, 500.f, 0xffffffff, 500.f, 75.f, 'B');
    uint32_t stroke_count = stbds_arrlen(E->strokes[E->strokes_idx]);
    if (stroke_count > 0) {
        int cap = stbds_arrcap(E->strokes[E->strokes_idx]);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glBindVertexArray(strokevao);
        glBindBuffer(GL_ARRAY_BUFFER, strokevbo);
        if (cap != stroke_capacity) {
            stroke_capacity = cap;
            glBindBuffer(GL_ARRAY_BUFFER, strokevbo);
            glBufferData(GL_ARRAY_BUFFER, stroke_capacity * sizeof(stroke_t), E->strokes[E->strokes_idx], GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            update_all_pattern_over_colors(G->patterns_map, E);
        } else {
            if (stroke_dirty_from < stroke_count) {
                glBufferSubData(GL_ARRAY_BUFFER, stroke_dirty_from * sizeof(stroke_t),
                                (stroke_count - stroke_dirty_from) * sizeof(stroke_t),
                                E->strokes[E->strokes_idx] + stroke_dirty_from);
                stroke_dirty_from = stroke_count;
                update_all_pattern_over_colors(G->patterns_map, E);
            }
        }
        check_gl("stroke draw post1");
        glUseProgram(stroke_prog);
        uniform2f(stroke_prog, "fScreenPx", (float)G->fbw, (float)G->fbh);
        uniform3f(stroke_prog, "center_and_zoom", E->centerx_sm, E->centery_sm, E->zoom_sm);
        check_gl("stroke draw post2");

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, stroke_count);
        check_gl("stroke draw post3");

        glBindVertexArray(0);
        unbind_textures_from_slots(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_BLEND);
        check_gl("stroke draw post");
    }
}
