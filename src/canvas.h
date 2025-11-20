GLuint strokevao = 0, strokevbo = 0;
int stroke_capacity = 1024;
int stroke_dirty_from = 0;


void init_stroke_buffer(void) {
    static const uint32_t attrib_sizes[2] = {4, 4};
    static const uint32_t attrib_types[2] = {GL_FLOAT,GL_UNSIGNED_BYTE};
    static const uint32_t attrib_offsets[2] = {offsetof(stroke_t, x), offsetof(stroke_t, col)};
    init_dynamic_buffer(1, &strokevao, &strokevbo, 2, attrib_sizes, attrib_types, attrib_offsets, stroke_capacity, sizeof(stroke_t));
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

void draw_canvas(EditorState *E) {
    update_zoom_and_center(E);
    if (E->strokes==NULL) {
        stroke_t *strokes = NULL;
        stbds_arrpush(E->strokes, strokes);
        E->strokes_idx = 0;
    }
    static double last_time = G->iTime;
    double dt = G->iTime - last_time;
    last_time = G->iTime;
    static double curx = 0, cury = 0;
    static bool was_drawing=false;

    double x = (G->mx - E->centerx_sm) / E->zoom_sm;
    double y = (G->my - E->centery_sm) / E->zoom_sm;
    RawPen *pen = mac_pen_get();
    bool mouse_down = G->mb || (pen && pen->down && pen->pressure > 1.f/255.f);
    int pressure = 255;
    if (pen) {
        // pen pos seems to lag glfw by a frame, so we trust the glfw mouse pos for now.
        // x = (pen->x * G->fbw - E->centerx_sm) / E->zoom_sm;
        // y = (pen->y * G->fbh - E->centery_sm) / E->zoom_sm;
        pressure = clamp((int)(sqrtf(pen->pressure) * 255.f), 1, 255);
        //add_line(pen->x * G->fbw, pen->y * G->fbh, pen->x * G->fbw, pen->y * G->fbh, 0xffff00ff, 500.f * pen->pressure);
    }

    if (!E->cur_inner_rad) E->cur_inner_rad = 30.f;
    if (!E->cur_outer_rad) E->cur_outer_rad = 40.f;
    if (E->cur_inner_rad+1.f > E->cur_outer_rad) E->cur_outer_rad = E->cur_inner_rad+1.f;
    float inner_rad = E->cur_inner_rad;
    float outer_rad = E->cur_outer_rad;
    int hover_idx = (G->my > G->fbh - 60.f) ? (G->mx-30.f) / 80.f + 0.5f : -1;
    if (hover_idx >= 9 || was_drawing) hover_idx = -1;

    if (hover_idx ==-1) {
        add_line(G->mx, G->my, G->mx, G->my, 0xffffffff, inner_rad * 2.f, -1.f); // brush mouse cursor
        add_line(G->mx, G->my, G->mx, G->my, 0x80808080, outer_rad * 2.f, -1.f); // brush mouse cursor
    }
    float pressure_to_size = max(0.25f, pressure / 255.f);
    inner_rad *= pressure_to_size;
    outer_rad *= pressure_to_size;

    // 181 160 32
    const static uint32_t cols[] = {0xffffffff, 0xff0000ee, 0xff00d0fc, 0xff00ee00, 0xfffcd000, 0xffee0000, 0xffd000fc, 0xff000000, 0};
    static float y_offsets[9] = {};
    for (int i=0;i<countof(cols);i++) {
        int c = cols[i];
        float x = 30.f + i*80.f;
        float y = G->fbh;
        float y_offset_target = (i==hover_idx) ? -15.f : 0.f;
        float radius = (i==E->cur_col_idx) ? 40.f : 30.f;
        y_offsets[i] += (y_offset_target - y_offsets[i]) * 0.1;
        y += y_offsets[i];
        add_line(x, y, x, y, c, radius*2.f);
        if (i>6) {
            add_line(x,y,x,y,0xffffffff, radius*2.f, -1.f);
        }
        if (i==8) {
            const float rsq = radius * 0.7071067812f - 1.f;
            add_line(x-rsq, y-rsq, x+rsq, y+rsq, 0xffffffff, 2.f);
            add_line(x-rsq, y+rsq, x+rsq, y-rsq, 0xffffffff, 2.f);
        }
        if (i==E->cur_col_idx) {
            add_line(x,y,x,y,0xffffffff, radius*2.f, -2.f);
        }

    }
    

    if (!mouse_down) {
        curx = x;
        cury = y;
        was_drawing=false;
    } else {
        if (hover_idx != -1) {
            E->cur_col_idx = hover_idx;
        } else {
            if (!was_drawing) {
                // append a copy of the current strokes to the undo history
                while (stbds_arrlen(E->strokes) > E->strokes_idx+1) {
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
                E->strokes_idx=stbds_arrlen(E->strokes)-1;
                printf("strokes_idx = %d\n", E->strokes_idx);
            }
            float dx = x - curx;
            float dy = y - cury;
            float stamp_every = max(2.f, max(outer_rad - inner_rad, inner_rad * 0.25f));
            
            float dist = sqrtf(square(dx) + square(dy)) / stamp_every * E->zoom_sm;
            if (dist>20.f) {
                int i=1;
            }
            if (!was_drawing) dist=max(dist,1.f);
            was_drawing=true;
            if (dist>=1.f) {
                dx /= dist;
                dy /= dist;
                while (dist >= 1.f) {
                    dist-=1.f;
                    curx+=dx;
                    cury+=dy;
                    if (E->cur_col_idx < 8) {
                        uint32_t cc = cols[E->cur_col_idx] & 0xffffff;
                        cc |= (pressure << 24);
                        stroke_t s = {(float)curx, (float)cury, inner_rad / E->zoom_sm, outer_rad / E->zoom_sm, cc};
                        stbds_arrpush(E->strokes[E->strokes_idx], s);
                        if ((stbds_arrlen(E->strokes[E->strokes_idx]) % 100)==0) {
                            printf("num strokes = %d\n", (int)stbds_arrlen(E->strokes[E->strokes_idx]));
                        }
                    } else {
                        // Find the closest entry in strokes and check for overlap.
                        for (int si = stbds_arrlen(E->strokes[E->strokes_idx]); si-->0;) {
                            const stroke_t *splat = &E->strokes[E->strokes_idx][si];
                            float dxs = (float)curx - splat->x;
                            float dys = (float)cury - splat->y;
                            float dist_sq = dxs * dxs + dys * dys;
                            float overlap_radius = (inner_rad / E->zoom_sm + splat->inner_rad) * 0.5f + 1.f/E->zoom_sm;
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
        if (cap!=stroke_capacity) {
            stroke_capacity = cap;
            glBindBuffer(GL_ARRAY_BUFFER, strokevbo);
            glBufferData(GL_ARRAY_BUFFER, stroke_capacity * sizeof(stroke_t), E->strokes[E->strokes_idx], GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        } else {
            if (stroke_dirty_from < stroke_count) {
                glBufferSubData(GL_ARRAY_BUFFER, stroke_dirty_from * sizeof(stroke_t), (stroke_count - stroke_dirty_from) * sizeof(stroke_t), E->strokes[E->strokes_idx] + stroke_dirty_from);
                stroke_dirty_from = stroke_count;
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

