// sample selector editor
// TODO - there are some instances of if editor_type == TAB_SAMPLES that should be cleaned up and moved in here.

void update_zoom_and_center(EditorState *E) {
    if (E->zoom==0.f) {
        E->zoom = 1.f;
        E->zoom_sm = 1.f;
    }
    if (is_two_finger_dragging() && G->mb == 0) {
        float dx = drag_cx - prev_drag_cx;
        float dy = drag_cy - prev_drag_cy;
        float velocity_sq = (dx * dx + dy * dy);
        float drag_sens = 1.f;
        
        E->centerx += (drag_cx - prev_drag_cx) * drag_sens;
        E->centery += (drag_cy - prev_drag_cy) * drag_sens;
        float mx_e = (G->mx - E->centerx) / E->zoom;
        float my_e = (G->my - E->centery) / E->zoom;
        float zoom_amount = expf(velocity_sq * -0.1f); // if we are moving the center of mass of fingers, supress zoom.
        E->zoom *= powf(drag_dist / prev_drag_dist, zoom_amount);
        E->zoom = clamp(E->zoom, 0.01f, 100.f);
        E->centerx = G->mx - mx_e * E->zoom;
        E->centery = G->my - my_e * E->zoom;
    }
    E->zoom = clamp(E->zoom, 0.01f, 100.f);
    E->zoom_sm += (E->zoom - E->zoom_sm) * 0.2f;
    E->centerx_sm += (E->centerx - E->centerx_sm) * 0.2f;
    E->centery_sm += (E->centery - E->centery_sm) * 0.2f;

}

void draw_umap(EditorState *E, uint32_t *ptr) {
    int tmw = G->fbw / E->font_width;
    int tmh = G->fbh / E->font_height;
    if (tmw > 512)
        tmw = 512;
    if (tmh > 256 - 8)
        tmh = 256 - 8;
    if (!E->embeddings) {
        sj_Reader r = read_json_file("assets/umap_sounds.json");
        for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
            sample_embedding_t t = {.key = iter_key_as_stbstring(&outer)};
            sj_iter_t inner = iter_start(&r, &outer.val);
            iter_next(&inner);
            t.x = iter_val_as_float(&inner, t.x);
            iter_next(&inner);
            t.y = iter_val_as_float(&inner, t.y);
            iter_next(&inner);
            int r = 128, g = 128, b = 128;
            r = clamp((int)(iter_val_as_float(&inner, r) * 255.f), 0, 255);
            iter_next(&inner);
            g = clamp((int)(iter_val_as_float(&inner, g) * 255.f), 0, 255);
            iter_next(&inner);
            b = clamp((int)(iter_val_as_float(&inner, b) * 255.f), 0, 255);
            t.col = (r << 16) | (g << 8) | b;
            iter_next(&inner);
            t.mindist = iter_val_as_float(&inner, t.mindist);
            iter_next(&inner);
            E->old_closest_idx = -1;
            int i;
            t.wave_idx = -1;
            t.sound_idx = -1;
            t.sound_number = 0;
            stbds_shputs(E->embeddings, t);
        }
        printf("loaded %d embeddings\n", (int)stbds_shlen(E->embeddings));
        // find mapping from sounds back to embeddings
        int ns = stbds_shlen(G->sounds);
        for (int i = 0; i < ns; ++i) {
            Sound *s = G->sounds[i].value;
            for (int j = 0; j < stbds_arrlen(s->wave_indices); ++j) {
                int wi = s->wave_indices[j];
                wave_t *w = &G->waves[wi];
                char name[1024];
                void sanitize_url_for_local_filesystem(const char *url, char *out, size_t cap);
                sanitize_url_for_local_filesystem(w->key, name, sizeof name);
                int embed_i = stbds_shgeti(E->embeddings, name);
                if (embed_i != -1) {
                    E->embeddings[embed_i].sound_idx = i;
                    E->embeddings[embed_i].sound_number = j;
                    E->embeddings[embed_i].wave_idx = wi;
                }
            }
        }
        E->zoom = 0.f;
        E->centerx = G->fbw / 2.f;
        E->centery = G->fbh / 2.f;
        free_json(&r);
    }
    // float extra_size = max(maxx - minx, maxy - miny) / sqrtf(1.f + num_after_filtering) * 0.125f;

    update_zoom_and_center(E);

    int n = stbds_shlen(E->embeddings);
    int closest_idx = 0;
    float closest_x = G->mx;
    float closest_y = G->my;
    float closest_d2 = 1e10;
    auto draw_point = [&](int i, float extra_size, bool matched) -> float2 {
        sample_embedding_t *e = &E->embeddings[i];
        int col = matched ? e->col | 0x60000000 : 0x10202020;
        float x = e->x * E->zoom_sm + E->centerx_sm;
        float y = e->y * E->zoom_sm + E->centery_sm;
        add_line(x, y, x, y, col, /*point_size*/ e->mindist * E->zoom_sm + extra_size);
        return float2{x, y};
    };
    int num_after_filtering = 0;
    int filtlen = find_end_of_line(E, 0);
    int new_filter_hash = fnv1_hash(E->str, E->str + filtlen);
    bool autozoom = (new_filter_hash != E->filter_hash) || E->zoom <= 0.f;
    E->filter_hash = new_filter_hash;
    float minx = 1e10, miny = 1e10;
    float maxx = -1e10, maxy = -1e10;
    int parsed_number = 0;
    const char *line = NULL;
    const char *colon = NULL;
    float fromt = 0.f;
    float tot = 1.f;
    if (E->cursor_y > 0) {
        // parse the line
        int start_idx = find_start_of_line(E, E->cursor_idx);
        int end_idx = find_end_of_line(E, E->cursor_idx);
        line = temp_cstring_from_span(E->str + start_idx, E->str + end_idx);
        colon = strchr(line, ':');
        if (!colon)
            colon = line + strlen(line);
        else
            parsed_number = atoi(colon + 1);
        const char *s = colon;
        while (*s && !isspace(*s))
            s++;
        const char *from = strstr(s, " from ");
        const char *to = strstr(s, " to ");
        if (from)
            fromt = clamp(atof(from + 5), 0.f, 1.f);
        if (to) {
            tot = clamp(atof(to + 3), 0.f, 1.f);
            if (!tot)
                tot = 1.f;
        }
        if (fromt >= tot) {
            float t = fromt;
            fromt = tot;
            tot = t;
        }
    }
    if (E->drag_type != DRAG_TYPE_NONE && G->mb == 1) {
        if (E->drag_type == DRAG_TYPE_CANVAS) {
            // E->centerx += G->mx - click_mx;
            // E->centery += G->my - click_my;
            // click_mx = G->mx;
            // click_my = G->my;
        } else {
            if (E->drag_type == DRAG_TYPE_SAMPLE_FROM) {
                fromt += (G->mx - click_mx) / (G->fbw - 96.f);
            } else if (E->drag_type == DRAG_TYPE_SAMPLE_TO) {
                tot += (G->mx - click_mx) / (G->fbw - 96.f);
            } else if (E->drag_type == DRAG_TYPE_SAMPLE_MIDDLE) {
                fromt += (G->mx - click_mx) / (G->fbw - 96.f);
                tot += (G->mx - click_mx) / (G->fbw - 96.f);
            }
            click_mx = G->mx;
            fromt = saturate(fromt);
            tot = saturate(tot);
            fromt = clamp(fromt, 0.f, tot);
            tot = clamp(tot, fromt, 1.f);
            if (E->cursor_y > 0) {
                int start_idx = find_start_of_line(E, E->cursor_idx);
                int end_idx = find_end_of_line(E, E->cursor_idx);
                char buf[1024];
                int n = snprintf(buf, sizeof(buf), "%s:%d from %0.5g to %0.5g", G->sounds[E->closest_sound_idx].value->name,
                                 E->closest_sound_number, fromt, tot);
                stbds_arrdeln(E->str, start_idx, end_idx - start_idx);
                stbds_arrinsn(E->str, start_idx, n);
                memcpy(E->str + start_idx, buf, n);
                E->cursor_idx = start_idx + n;
                E->select_idx = E->cursor_idx;
            }
        }
    }

    ////////////// draw it!

    float2 matched_p = float2{0.f, 0.f};
    for (int i = 0; i < n; ++i) {
        sample_embedding_t *e = &E->embeddings[i];
        if (e->sound_idx == -1)
            continue;
        const char *soundname = G->sounds[e->sound_idx].value->name;
        char soundname_with_colon[1024];
        snprintf(soundname_with_colon, sizeof soundname_with_colon, "%s:%d", soundname, e->sound_number);
        bool matched = true;
        if (E->cursor_y > 0) {
            matched = strlen(soundname) == colon - line && strncasecmp(soundname, line, colon - line) == 0 &&
                      parsed_number == e->sound_number;
        } else if (filtlen) {
            matched = false;
            // find the words separated by whitespace in E->str
            const char *ws = E->str;
            const char *end = E->str + filtlen;
            while (ws < end) {
                while (ws < end && isspace(*ws))
                    ws++;
                const char *we = ws;
                while (we < end && !isspace(*we))
                    we++;
                if (we > ws) {
                    for (const char *s = soundname_with_colon; *s; s++) {
                        if (strncasecmp(s, ws, we - ws) == 0) {
                            matched = true;
                            break;
                        }
                    }
                }
                ws = we;
            }
        }
        bool matched_or_shift = matched || ((last_mods & GLFW_MOD_SHIFT) && E->cursor_y == 0);
        float2 p = draw_point(i, 1.f, matched_or_shift);
        if (matched) {
            minx = min(minx, e->x);
            miny = min(miny, e->y);
            maxx = max(maxx, e->x);
            maxy = max(maxy, e->y);
            matched_p = p;
            num_after_filtering++;
        }
        if (matched_or_shift) {
            float d2 = square(G->mx - p.x) + square(G->my - p.y);
            if (d2 < closest_d2) {
                closest_d2 = d2;
                closest_idx = i;
                closest_x = p.x;
                closest_y = p.y;
            }
        }
    }
    draw_point(closest_idx, 30.f, true);
    if (G->mb == 0 && (E->old_closest_idx != closest_idx || fromt != G->preview_fromt || tot != G->preview_tot)) {
        if (G->preview_wave_fade > 0.01f) {
            G->preview_wave_fade *= 0.999f; // start the fade
        } else {
            int wi = E->embeddings[closest_idx].wave_idx;
            wave_t *w = &G->waves[wi];
            if (wi != -1)
                request_wave_load(w);
            if (w->num_frames) {
                E->old_closest_idx = closest_idx;
                E->closest_sound_idx = E->embeddings[closest_idx].sound_idx;
                E->closest_sound_number = E->embeddings[closest_idx].sound_number;
                G->preview_fromt = fromt;
                G->preview_tot = tot;
                G->preview_wave_t = 0.;
                G->preview_wave_idx_plus_one = wi + 1;
                G->preview_wave_fade = 1.f;
            }
        }
    }
    if (E->closest_sound_idx != -1) {
        Sound *s = G->sounds[E->closest_sound_idx].value;
        const char *name = s->name;
        print_to_screen(ptr, closest_x / E->font_width + 2.f, closest_y / E->font_height - 0.5f, C_SELECTION, false, "%s:%d", name,
                        E->closest_sound_number);
        if (closest_idx != -1) {
            sample_embedding_t *e = &E->embeddings[closest_idx];
            wave_t *w = &G->waves[e->wave_idx];
            int smp0 = 0;
            float startx = 48.f + fromt * (G->fbw - 96.f);
            float endx = 48.f + tot * (G->fbw - 96.f);
            if (w->num_frames) {
                float playpos_frac = fromt + (G->preview_wave_t * w->sample_rate / SAMPLE_RATE / w->num_frames);
                if (playpos_frac >= fromt && playpos_frac < tot) {
                    float playx = 48.f + playpos_frac * (G->fbw - 96.f);
                    add_line(playx, G->fbh - 256.f, playx, G->fbh, 0xffffffff, 4.f);
                }
            }
            add_line(startx, G->fbh - 256.f, startx, G->fbh, 0xffeeeeee, 3.f);
            add_line(endx, G->fbh - 256.f, endx, G->fbh, 0xffeeeeee, 3.f);
            for (int x = 48; x < G->fbw - 48.f; ++x) {
                float f = (x - 48.f) / (G->fbw - 96.f);
                int smp1 = ((int)(f * w->num_frames)) * w->channels;
                float mn = 1e10, mx = -1e10;
                for (int s = smp0; s < smp1; ++s) {
                    float v = w->frames[s];
                    mn = min(mn, v);
                    mx = max(mx, v);
                }
                float ymid = G->fbh - 128.f;
                add_line(x, ymid + mn * 128.f, x, ymid + mx * 128.f, (f >= fromt && f < tot) ? e->col : 0x40404040, 2.f);
                smp0 = smp1;
            }
        }
    }
    if (num_after_filtering == 1 &&
        (matched_p.x < 32.f || matched_p.x > G->fbw - 32.f || matched_p.y < 32.f || matched_p.y > G->fbh - 32.f)) {
        autozoom = true;
    }
    if (autozoom) {
        if (num_after_filtering > 1) {
            E->zoom = min(G->fbw * 0.75f / (maxx - minx + 10.f), G->fbh * 0.9f / (maxy - miny + 10.f));
        }
        E->zoom = clamp(E->zoom, 0.01f, 100.f);
        E->centerx = G->fbw / 2.f - (maxx + minx) / 2.f * E->zoom;
        E->centery = G->fbh / 2.f - (maxy + miny) / 2.f * E->zoom;
    }
}
