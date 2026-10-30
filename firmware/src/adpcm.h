#pragma once
#include <FS.h>

// Converts the rabbit's microphone recordings (IMA ADPCM WAV, mono 8 kHz,
// 256-byte blocks) into 16-bit PCM WAV, which browsers can play.
// Returns false (and leaves dst alone) if src isn't IMA ADPCM.
bool adpcmToPcmWav(fs::FS &fs, const char *src, const char *dst);
