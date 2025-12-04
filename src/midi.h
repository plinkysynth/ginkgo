#pragma once
typedef void (*midi_receive_cb)(uint8_t data[3], void *user);
void midi_send(uint8_t data0, uint8_t data1, uint8_t data2);
void midi_init(midi_receive_cb cb, void *cb_user);

uint8_t plinky12_connected = 0;
uint8_t mu8_connected = 0  ;
uint8_t plinky12_leds[16][16] = {0};
uint8_t plinky12_leds_sent[16][16] = {0};
uint8_t plinky12_pressures[16][16] = {0};
uint16_t plinky12_down[16] = {0}; // by COLUMN, ie index by x
uint8_t hot_led = 0; // we slowly cycle through the leds and update at least this one per frame, in order to update a freshly rebooted device.
float plinky12_scale_root = 0;
uint32_t plinky12_scale_bits = 0;
int plinky12_octave = 0;
void on_midi_input(uint8_t data[3], void *user) {
    if (!G)
        return;
    int cc = data[1];
    int chan = data[0]&0xf;
    int type = data[0]>>4;
    //printf("midi: %02x %02x %02x\n", data[0], data[1], data[2]);
    if (type==10 || type==9 || type==8) {
        int note = data[1]-60;
        if (plinky12_connected && note>=0 && note<16) {
            uint8_t old_pressure = plinky12_pressures[note][chan];
            plinky12_pressures[note][chan] = data[2];
            if (data[2]>0)
                plinky12_down[chan] |= (1<<note);
            else
                plinky12_down[chan] &= ~(1<<note);
            if (chan>=8 && note>8) {
                // slider
                int tot=0,ytot=0;
                for (int y=0;y<7;y++) {
                    int p = plinky12_pressures[15-y][chan];
                    tot+=p; ytot+=y*p; 
                }
                if (tot > 48) {
                    ytot=(ytot*127)/(tot*7);
                    G->midi_cc[16 + note-8] = ytot;
                }
            }
            if (!old_pressure && data[2]>0) {
                if (chan==8 && note == 0 && plinky12_octave>0) {
                    plinky12_octave--;
                }
                else if (chan==9 && note == 0 && plinky12_octave<3) {
                    plinky12_octave++;
                }
                else if (chan==15 && note == 0) {
                    G->playing = !G->playing;
                }
                else if (note>0 && note<=8 && chan>=8) {
                    G->mutes ^= (1<<(chan-8));
                }   
            }
        }
    }
    if (data[0] == 0xb0 && cc < 128) {
        int oldccdata = G->midi_cc[cc];
        int newccdata = data[2];
        G->midi_cc[cc] = newccdata;
        /*
        uint32_t gen = G->midi_cc_gen[cc]++;
        if (gen == 0)
            oldccdata = newccdata;
        if (newccdata != oldccdata && cc >= 16 && cc < 32 && closest_slider[cc - 16] != NULL) {
            // 'pickup': if we are increasing and bigger, or decreasing and smaller, then pick up the value, or closer than 2
            int sliderval = (int)clamp(closest_slider[cc - 16][0] * 127.f, 0.f, 127.f);
            int mindata = min(oldccdata, newccdata);
            int maxdata = max(oldccdata, newccdata);
            int vel = newccdata - oldccdata;
            if (vel < 0)
                maxdata += (-vel) + 16;
            else
                mindata -= (vel) + 16; // add slop for velocity
            if (sliderval >= mindata - 4 && sliderval <= maxdata + 4) {
                closest_slider[cc - 16][0] = newccdata / 127.f;
            }
        }
            */
    }
    // printf("midi: %02x %02x %02x\n", data[0], data[1], data[2]);
}

void update_plinky12_leds(void) {
    if (!plinky12_connected)
        return;
    for (int y=0;y<16;y++) {
        for (int x=0;x<16;x++) {
            if (plinky12_leds[y][x] != plinky12_leds_sent[y][x] || hot_led == y*16 + x) {
                uint8_t col = plinky12_leds[y][x];
                if (col&128)
                    midi_send(0xb0+x, 32+y, (col&15)+(col/32)*16); // double brightness
                else
                    midi_send(0xb0+x, 16+y, col);
            }
        }
    }
    hot_led = (hot_led + 1);
}

#ifdef __APPLE__

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <string.h>
#include "ansicols.h"


struct {
    MIDIClientRef client;
    MIDIPortRef in_port, out_port;
    MIDIEndpointRef in_src, out_dst;
    midi_receive_cb cb;
    void *cb_user;
} midi_state;

 void midi_input_proc(const MIDIPacketList *pktlist, void *ref, void *src) {
    midi_receive_cb cb = midi_state.cb;
    if (!cb)
        return;

    const MIDIPacket *pkt = &pktlist->packet[0];
    for (int i = 0; i < pktlist->numPackets; ++i) {
        const uint8_t *data = pkt->data;
        for (int j = 0; j + 2 < pkt->length; j += 3) {
            cb((uint8_t[]){data[j], data[j + 1], data[j + 2]}, midi_state.cb_user);
        }
        pkt = MIDIPacketNext(pkt);
    }
}

int midi_get_num_inputs() { return (int)MIDIGetNumberOfSources(); }

int midi_get_num_outputs() { return (int)MIDIGetNumberOfDestinations(); }

void midi_open_input(int index) {
    MIDIEndpointRef src = MIDIGetSource(index);
    if (src) {
        midi_state.in_src = src;
        MIDIPortConnectSource(midi_state.in_port, src, NULL);
    }
}

void midi_open_output(int index) {
    MIDIEndpointRef dst = MIDIGetDestination(index);
    if (dst)
        midi_state.out_dst = dst;
}

void midi_send(uint8_t data0, uint8_t data1, uint8_t data2) {
    if (!midi_state.out_dst)
        return;
    Byte buffer[3 + 100];
    MIDIPacketList *pktlist = (MIDIPacketList *)buffer;
    MIDIPacket *pkt = MIDIPacketListInit(pktlist);
    uint8_t data[3] = {data0, data1, data2};
    MIDIPacketListAdd(pktlist, sizeof(buffer), pkt, 0, 3, data);
    MIDISend(midi_state.out_port, midi_state.out_dst, pktlist);
}

const char *midi_get_input_name(int index) {
    static char namebuf[256];
    namebuf[0] = 0;
    MIDIEndpointRef src = MIDIGetSource(index);
    if (src) {
        CFStringRef pname;
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &pname);
        CFStringGetCString(pname, namebuf, sizeof(namebuf), kCFStringEncodingUTF8);
        CFRelease(pname);
    }
    return namebuf;
}

const char *midi_get_output_name(int index) {
    static char namebuf[256];
    namebuf[0] = 0;
    MIDIEndpointRef dst = MIDIGetDestination(index);
    if (dst) {
        CFStringRef pname;
        MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &pname);
        CFStringGetCString(pname, namebuf, sizeof(namebuf), kCFStringEncodingUTF8);
        CFRelease(pname);
    }
    return namebuf;
}

void midi_init(midi_receive_cb cb, void *cb_user) {
    MIDIClientCreate(CFSTR("midi"), NULL, NULL, &midi_state.client);
    MIDIInputPortCreate(midi_state.client, CFSTR("in"), midi_input_proc, NULL, &midi_state.in_port);
    MIDIOutputPortCreate(midi_state.client, CFSTR("out"), &midi_state.out_port);
    midi_state.cb = cb;
    midi_state.cb_user = cb_user;
    int ni = midi_get_num_inputs();
    int no = midi_get_num_outputs();
    int in_idx = 0;
    int out_idx = 0;

    printf(COLOR_CYAN "%d" COLOR_RESET " MIDI inputs found, " COLOR_CYAN "%d" COLOR_RESET " MIDI outputs found\n", ni, no);
    for (int i = 0; i < ni + no; i++) {
        const char *name = i < ni ? midi_get_input_name(i) : midi_get_output_name(i - ni);
        printf("\t" COLOR_CYAN "%s%d" COLOR_RESET " - %s\n", i < ni ? "in" : "out", i, name);
        if (strcmp(name, "plinky12") == 0) {
            if (i < ni)
                in_idx = i;
            else
                out_idx = i - ni;
            plinky12_connected |= (i < ni) ? 1 : 2;
            mu8_connected = 0;
        }
        if (strstr(name, "Music Thing Modular")) {
            if (i < ni)
                in_idx = i;
            else
                out_idx = i - ni;
            plinky12_connected = 0;
            mu8_connected |= (i < ni) ? 1 : 2;
        }
    }
    if (plinky12_connected < 3)
        plinky12_connected = 0;
    else
        printf(COLOR_RED "plinky12 connected" COLOR_RESET "\n");
    if (mu8_connected < 3)
        mu8_connected = 0;
    else
        printf(COLOR_RED "mu8 connected" COLOR_RESET "\n");
    if (ni > 0) {
        const char *name = midi_get_input_name(in_idx);
        printf(COLOR_CYAN "using midi input: %s" COLOR_RESET "\n", name);
        midi_open_input(in_idx);
    }
    if (no > 0) {
        const char *name = midi_get_output_name(out_idx);
        printf(COLOR_CYAN "using midi output: %s" COLOR_RESET "\n", name);
        midi_open_output(out_idx);
    }
}

#endif // __APPLE__
#ifdef __LINUX__
#pragma once

#include <stdint.h>
#include <alsa/asoundlib.h>
#include <pthread.h>



struct {
    snd_rawmidi_t *in;
    snd_rawmidi_t *out;
    midi_receive_cb cb;
    void *cb_user;
    int running;
    pthread_t thread;
} midi_state = {0};

void *midi_read_thread(void *arg) {
    (void)arg;
    uint8_t buf[3];
    int n = 0;

    while (midi_state.running && midi_state.in) {
        unsigned char b;
        int r = snd_rawmidi_read(midi_state.in, &b, 1);
        if (r <= 0)
            continue; // ignore errors for simplicity

        buf[n++] = (uint8_t)b;
        if (n == 3) {
            midi_receive_cb cb = midi_state.cb;
            if (cb)
                cb(buf, midi_state.cb_user);
            n = 0;
        }
    }
    return NULL;
}

void midi_init(midi_receive_cb cb, void *cb_user) {
    midi_state.cb = cb;
    midi_state.cb_user = cb_user;
    midi_state.in = NULL;
    midi_state.out = NULL;
    midi_state.running = 0;

    // try to open hw:1,0,0 for both in and out; if it fails, we just stay inactive
    // TODO implement match_name
    if (snd_rawmidi_open(&midi_state.in, &midi_state.out, "hw:1,0,0", 0) < 0) {
        midi_state.in = NULL;
        midi_state.out = NULL;
        return;
    }

    midi_state.running = 1;
    pthread_create(&midi_state.thread, NULL, midi_read_thread, NULL);
}

void midi_send(uint8_t data0, uint8_t data1, uint8_t data2) {
    if (!midi_state.out)
        return;
    uint8_t data[3] = {data0, data1, data2};
    snd_rawmidi_write(midi_state.out, data, 3);
    snd_rawmidi_drain(midi_state.out);
}

#endif
#ifdef __WINDOWS__

#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")



struct {
    HMIDIIN in;
    HMIDIOUT out;
    midi_receive_cb cb;
    void *cb_user;
} midi_state = {0};

void CALLBACK midi_in_proc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hMidiIn;
    (void)dwInstance;
    (void)dwParam2;
    if (wMsg != MIM_DATA)
        return;
    midi_receive_cb cb = midi_state.cb;
    if (!cb)
        return;

    DWORD msg = (DWORD)dwParam1;
    uint8_t b0 = (uint8_t)(msg & 0xFF);
    uint8_t b1 = (uint8_t)((msg >> 8) & 0xFF);
    uint8_t b2 = (uint8_t)((msg >> 16) & 0xFF);

    uint8_t data[3] = {b0, b1, b2};
    cb(data, midi_state.cb_user);
}

void midi_init(midi_receive_cb cb, void *cb_user) {
    midi_state.cb = cb;
    midi_state.cb_user = cb_user;
    midi_state.in = NULL;
    midi_state.out = NULL;

    UINT in_count = midiInGetNumDevs();
    UINT out_count = midiOutGetNumDevs();

    UINT in_index = 0, out_index = 0;

    if (match_name && match_name[0]) {
        // try to find first input that matches
        for (UINT i = 0; i < in_count; ++i) {
            MIDIINCAPS caps;
            if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                if (strstr(caps.szPname, "Music Thing Modular")) {
                    in_index = i;
                    break;
                }
            }
        }
        // try to find first output that matches
        for (UINT i = 0; i < out_count; ++i) {
            MIDIOUTCAPS caps;
            if (midiOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
                if (strstr(caps.szPname, match_name)) {
                    out_index = i;
                    break;
                }
            }
        }
    }

    if (in_count > 0) {
        if (midiInOpen(&midi_state.in, in_index, (DWORD_PTR)midi_in_proc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            midi_state.in = NULL;
        } else {
            midiInStart(midi_state.in);
        }
    }

    if (out_count > 0) {
        if (midiOutOpen(&midi_state.out, out_index, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            midi_state.out = NULL;
        }
    }
}

void midi_send(uint8_t data0, uint8_t data1, uint8_t data2) {
    if (!midi_state.out)
        return;
    DWORD msg = (DWORD)data0 | ((DWORD)data1 << 8) | ((DWORD)data2 << 16);
    midiOutShortMsg(midi_state.out, msg);
}

#endif // __WINDOWS__
