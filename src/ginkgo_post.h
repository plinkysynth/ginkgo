
extern "C" {
    size_t get_state_size(void) { return sizeof(song); }                                                                          \
    stereo do_sample(stereo inp) {
        return ((song *)G)->do_sample(inp);
    }

}
