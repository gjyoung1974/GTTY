add_library(gttycore STATIC
  baudot.cpp
  version.cpp
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
  rig/serial_ptt.cpp
  rig/rig_decode_port.cpp
  rig/serial_ports.cpp
  log/logbook.cpp
  log/adif.cpp
  log/callsign.cpp
  log/rx_highlight.cpp
  log/grid.cpp
  log/callbook.cpp
  net/wsjtx_udp.cpp
  net/xmlrpc_server.cpp
  net/cluster_client.cpp
  net/fllog_client.cpp
  net/xlog_client.cpp
  net/spot_server.cpp
  net/psk_reporter.cpp
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
if(ALSA_FOUND)
  target_link_libraries(gttycore PRIVATE PkgConfig::ALSA)
  target_compile_definitions(gttycore PRIVATE GTTY_HAVE_ALSA=1)
endif()
if(CURL_FOUND)
  target_link_libraries(gttycore PUBLIC PkgConfig::CURL)
  target_compile_definitions(gttycore PRIVATE GTTY_HAVE_CURL=1)
endif()

# version.cpp includes the header the gtty_version target writes from git.
target_include_directories(gttycore PRIVATE ${GTTY_GENERATED_DIR})
add_dependencies(gttycore gtty_version)
