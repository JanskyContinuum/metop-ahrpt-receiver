/* Original fixture generator. Requires the external libfec encoder only.
 * Not part of the decoder or its build; see README.md for provenance. */
#include "fec.h"
#include <stdio.h>
#include <string.h>

static void hex_line(FILE *out, const unsigned char *bytes, unsigned size) {
    unsigned i;
    for (i = 0; i < size; ++i) fprintf(out, "%02x", bytes[i]);
    fputc('\n', out);
}
static void packet(unsigned char *out, unsigned size, unsigned apid, unsigned salt) {
    unsigned i;
    for (i = 0; i < size; ++i) out[i] = (unsigned char)(i * 37 + salt);
    out[0] = (unsigned char)(apid >> 8); out[1] = (unsigned char)apid;
    out[2] = 0xc0; out[3] = 0;
    out[4] = (unsigned char)((size - 7) >> 8); out[5] = (unsigned char)(size - 7);
}
int main(int argc, char **argv) {
    unsigned char word[255], stream[2646], frame[1020];
    unsigned i, lane, index;
    FILE *out;
    if (argc != 3) return 2;
    out = fopen(argv[1], "wb");
    if (!out) return 1;
    for (index = 0; index < 2; ++index) {
        for (i = 0; i < 223; ++i) word[i] = index == 0 ? (unsigned char)i : 0xff;
        encode_rs_ccsds(word, word + 223, 0);
        hex_line(out, word, 255);
    }
    if (fclose(out)) return 1;
    packet(stream, 2000, 103, 17); packet(stream + 2000, 646, 104, 29);
    out = fopen(argv[2], "wb");
    if (!out) return 1;
    for (index = 0; index < 4; ++index) {
        memset(frame, 0, sizeof(frame));
        frame[0] = 0x43; frame[1] = 9; frame[4] = (unsigned char)index;
        if (index < 3) memcpy(frame + 10, stream + index * 882, 882);
        else packet(frame + 10, 882, 105, 43);
        if (index == 1) { frame[8] = 7; frame[9] = 0xff; }
        if (index == 2) frame[9] = 236;
        for (lane = 0; lane < 4; ++lane) {
            for (i = 0; i < 223; ++i) word[i] = frame[4 * i + lane];
            encode_rs_ccsds(word, word + 223, 0);
            for (i = 223; i < 255; ++i) frame[4 * i + lane] = word[i];
        }
        hex_line(out, frame, 1020);
    }
    return fclose(out) ? 1 : 0;
}
