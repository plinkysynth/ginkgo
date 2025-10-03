#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t sample_rate;
  uint16_t num_channels;
  uint16_t bits_per_sample;
  uint16_t audio_format;
  uint32_t data_start;
  uint32_t data_end;
} WAVInfo;


static bool parse_wav_file(FILE *file, WAVInfo *info) {
  uint8_t buffer[64]; // Temporary buffer for reading chunks
  uint32_t chunk_size;
  info->sample_rate = 0;
  info->data_start = 0;
  info->data_end = 0;
  fseek(file, 0, SEEK_SET);
  if (fread(buffer, 1,12, file) != 12) {
    return false;
  }
  if (memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer + 8, "WAVE", 4) != 0) {
    return false; // Not a valid WAV file
  }
  // Parse chunks until "data" chunk is found
  while (!feof(file)) {
    if (fread(buffer, 1, 8, file) != 8) {
      return false;
    }
    chunk_size = *(uint32_t *)(buffer + 4);
    int chunk_start = ftell(file);
    //printf("wav chunk %c%c%c%c %u\n", buffer[0], buffer[1], buffer[2], buffer[3], (unsigned int)chunk_size);
    // Check for "fmt " chunk
    if (memcmp(buffer, "fmt ", 4) == 0 && chunk_size <= sizeof(buffer)) {
      if (fread(buffer, 1, chunk_size, file) != chunk_size) {
        return false;
      }
      info->num_channels = *(uint16_t *)(buffer + 2);
      info->sample_rate = *(uint32_t *)(buffer + 4);
      info->bits_per_sample = *(uint16_t *)(buffer + 14);
      info->audio_format = *(uint16_t *)(buffer);
      printf("wav fmt %d chans, %d samplerate, %d bits per sample, format %d\n", 
             (int)info->num_channels, (int)info->sample_rate,
             (int)info->bits_per_sample, (int)info->audio_format);
    } else if (memcmp(buffer, "data", 4) == 0) {
      // Found "data" chunk
      info->data_start = ftell(file);
      info->data_end = info->data_start + chunk_size;
      //printf("wav data offset %d to %d\n", (int)info->data_start, (int)info->data_end);
      return (info->sample_rate != 0 && (info->audio_format == 1 || info->audio_format == 3));
    }
    // Skip over the chunk if not "fmt " or "data"
    if (fseek(file, chunk_start + chunk_size + (chunk_size & 1), SEEK_SET) != 0) {
      return false;
    }
  }
  return false; // Should not reach here
}

static inline void write_wav_header(FILE *file, int n_samples, int sample_rate, int channels) {
  uint8_t header[44] = {
    'R', 'I', 'F', 'F',                     // ChunkID
    0, 0, 0, 0,                             // ChunkSize (to be filled)
    'W', 'A', 'V', 'E',                     // Format
    'f', 'm', 't', ' ',                     // Subchunk1ID
    16, 0, 0, 0,                            // Subchunk1Size (16 for PCM or IEEE float)
    3, 0,                                    // AudioFormat (3 for IEEE float)
    (uint8_t)channels, 0,                             // NumChannels
    0, 0, 0, 0,                             // SampleRate (to be filled)
    0, 0, 0, 0,                             // ByteRate (to be filled)
    (uint8_t)(4*channels), 0,                          // BlockAlign (4 bytes per sample per channel)
    32, 0,                                   // BitsPerSample (32)
    'd', 'a', 't', 'a',                     // Subchunk2ID
    0, 0, 0, 0                              // Subchunk2Size (to be filled)
  };

  // Fill in the dynamic values
  uint32_t data_size = n_samples * channels * 4; // 4 bytes per sample (32-bit float)
  uint32_t chunk_size = data_size + 36; // 36 = header size - 8
  *(uint32_t*)(header + 4) = chunk_size;
  *(uint32_t*)(header + 24) = sample_rate;
  *(uint32_t*)(header + 28) = sample_rate * 4 * channels; // ByteRate = SampleRate * NumChannels * BitsPerSample/8
  *(uint32_t*)(header + 40) = data_size;
  fwrite(header, sizeof(header), 1, file);
}

#ifdef __cplusplus
}

#endif

