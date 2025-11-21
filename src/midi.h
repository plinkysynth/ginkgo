#pragma once
#ifdef __APPLE__

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <string.h>

typedef void (*midi_receive_cb)(uint8_t data[3], void *user);

static struct {
    MIDIClientRef client;
    MIDIPortRef in_port, out_port;
    MIDIEndpointRef in_src, out_dst;
    midi_receive_cb cb;
    void *cb_user;
} midi_state;

static void midi_input_proc(const MIDIPacketList *pktlist, void *ref, void *src) {
    midi_receive_cb cb = midi_state.cb;
    if (!cb) return;

    const MIDIPacket *pkt = &pktlist->packet[0];
    for (int i = 0; i < pktlist->numPackets; ++i) {
        const uint8_t *data = pkt->data;
        for (int j = 0; j + 2 < pkt->length; j += 3) {
            cb((uint8_t[]){data[j], data[j+1], data[j+2]}, midi_state.cb_user);
        }
        pkt = MIDIPacketNext(pkt);
    }
}


static int midi_get_num_inputs() {
    return (int)MIDIGetNumberOfSources();
}

static int midi_get_num_outputs() {
    return (int)MIDIGetNumberOfDestinations();
}

static void midi_open_input(int index) {
    MIDIEndpointRef src = MIDIGetSource(index);
    if (src) {
        midi_state.in_src = src;
        MIDIPortConnectSource(midi_state.in_port, src, NULL);
    }
}

static void midi_open_output(int index) {
    MIDIEndpointRef dst = MIDIGetDestination(index);
    if (dst) midi_state.out_dst = dst;
}



static void midi_send(uint8_t data[3]) {
    if (!midi_state.out_dst) return;
    Byte buffer[3 + 100];
    MIDIPacketList *pktlist = (MIDIPacketList*)buffer;
    MIDIPacket *pkt = MIDIPacketListInit(pktlist);
    MIDIPacketListAdd(pktlist, sizeof(buffer), pkt, 0, 3, data);
    MIDISend(midi_state.out_port, midi_state.out_dst, pktlist);
}

static const char* midi_get_input_name(int index) {
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

static const char* midi_get_output_name(int index) {
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

static void midi_init(const char *match_name ,midi_receive_cb cb, void *cb_user) {
    MIDIClientCreate(CFSTR("midi"), NULL, NULL, &midi_state.client);
    MIDIInputPortCreate(midi_state.client, CFSTR("in"), midi_input_proc, NULL, &midi_state.in_port);
    MIDIOutputPortCreate(midi_state.client, CFSTR("out"), &midi_state.out_port);
    midi_state.cb = cb;
    midi_state.cb_user = cb_user;
    int ni = midi_get_num_inputs();
    int idx = 0;
    for (int i = 0; i < ni; i++) {
        const char *name = midi_get_input_name(i);
        if (strstr(name, match_name)) {
            idx = i;
            break;
        }
    }
    if (ni>0) midi_open_input(idx);
}

#endif // __APPLE__
#ifdef __LINUX__
#pragma once

#include <stdint.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

typedef void (*midi_receive_cb)(uint8_t data[3], void *user);

static struct {
    snd_rawmidi_t *in;
    snd_rawmidi_t *out;
    midi_receive_cb cb;
    void *cb_user;
    int running;
    pthread_t thread;
} midi_state = {0};

static void* midi_read_thread(void *arg) {
    (void)arg;
    uint8_t buf[3];
    int n = 0;

    while (midi_state.running && midi_state.in) {
        unsigned char b;
        int r = snd_rawmidi_read(midi_state.in, &b, 1);
        if (r <= 0) continue; // ignore errors for simplicity

        buf[n++] = (uint8_t)b;
        if (n == 3) {
            midi_receive_cb cb = midi_state.cb;
            if (cb) cb(buf, midi_state.cb_user);
            n = 0;
        }
    }
    return NULL;
}

static void midi_init(const char *match_name ,midi_receive_cb cb, void *cb_user) {
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

static void midi_send(uint8_t data[3]) {
    if (!midi_state.out) return;
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

typedef void (*midi_receive_cb)(uint8_t data[3], void *user);

static struct {
    HMIDIIN in;
    HMIDIOUT out;
    midi_receive_cb cb;
    void *cb_user;
} midi_state = {0};

static void CALLBACK midi_in_proc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                  DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)hMidiIn; (void)dwInstance; (void)dwParam2;
    if (wMsg != MIM_DATA) return;
    midi_receive_cb cb = midi_state.cb;
    if (!cb) return;

    DWORD msg = (DWORD)dwParam1;
    uint8_t b0 = (uint8_t)(msg & 0xFF);
    uint8_t b1 = (uint8_t)((msg >> 8) & 0xFF);
    uint8_t b2 = (uint8_t)((msg >> 16) & 0xFF);

    uint8_t data[3] = { b0, b1, b2 };
    cb(data, midi_state.cb_user);
}

static void midi_init(const char *match_name, midi_receive_cb cb, void *cb_user) {
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
                if (strstr(caps.szPname, match_name)) {
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

static void midi_send(uint8_t data[3]) {
    if (!midi_state.out) return;
    DWORD msg = (DWORD)data[0] | ((DWORD)data[1] << 8) | ((DWORD)data[2] << 16);
    midiOutShortMsg(midi_state.out, msg);
}

#endif // __WINDOWS__
