add_library(gttycore STATIC
  baudot.cpp
  dsp/resampler.cpp
  dsp/spectrum.cpp
  modem/rtty_modulator.cpp
  modem/matched_filter_demod.cpp
  modem/decoder_bank.cpp
  modem/correlator_demod.cpp
  modem/codec2_demod.cpp
  modem/voter.cpp
  modem/skimmer.cpp
  modem/cw.cpp
  audio/audio_engine.cpp
  audio/wav_file.cpp
  rig/rig_control.cpp
  rig/fsk_keyer.cpp
  log/logbook.cpp
  log/callsign.cpp
  log/grid.cpp
  log/callbook.cpp
  net/wsjtx_udp.cpp
  net/xmlrpc_server.cpp
  net/cluster_client.cpp
  macros.cpp
  transmitter.cpp
  receiver.cpp
)
target_include_directories(gttycore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gttycore PUBLIC
  PkgConfig::HAMLIB PkgConfig::PORTAUDIO PkgConfig::FFTW3F
  PkgConfig::SAMPLERATE PkgConfig::SNDFILE PkgConfig::SQLITE3
  Threads::Threads)
if(CODEC2_FOUND)
  target_link_libraries(gttycore PUBLIC PkgConfig::CODEC2)
  target_compile_definitions(gttycore PRIVATE GTTY_HAVE_CODEC2=1)
  # fsk.h includes kiss_fft.h, which the Debian package omits; see codec2_compat/.
  set_source_files_properties(modem/codec2_demod.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/modem/codec2_compat")
endif()
if(CURL_FOUND)
  target_link_libraries(gttycore PUBLIC PkgConfig::CURL)
  target_compile_definitions(gttycore PRIVATE GTTY_HAVE_CURL=1)
endif()
