
extern "C" {
    size_t get_state_size(void) { return sizeof(song); }  
    void init_state(void) { ((song*)G)->init(); }                                                                        \
    stereo do_sample(stereo inp) {
        return ((song *)G)->do_sample(inp);
    }

}
