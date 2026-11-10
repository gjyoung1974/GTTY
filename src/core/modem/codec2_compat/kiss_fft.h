// Minimal stand-in for kiss_fft.h, which libcodec2's fsk.h includes (as
// kiss_fftr.h from 1.2 on) but which the Debian and Homebrew packages do not
// ship. struct FSK only holds an opaque kiss_fft_cfg pointer, so this typedef
// keeps the layout identical to the real header.
#pragma once
struct kiss_fft_state;
typedef struct kiss_fft_state* kiss_fft_cfg;
