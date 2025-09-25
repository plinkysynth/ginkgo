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

static void midi_init(midi_receive_cb cb, void *cb_user) {
    MIDIClientCreate(CFSTR("midi"), NULL, NULL, &midi_state.client);
    MIDIInputPortCreate(midi_state.client, CFSTR("in"), midi_input_proc, NULL, &midi_state.in_port);
    MIDIOutputPortCreate(midi_state.client, CFSTR("out"), &midi_state.out_port);
    midi_state.cb = cb;
    midi_state.cb_user = cb_user;
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

#endif // __APPLE__
