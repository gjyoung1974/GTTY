// Finding the saved sound card again, and the names shown for sound cards in Preferences.
#include <string>
#include <vector>

#include "core/audio/audio_engine.h"
#include "check.h"

using namespace gtty;

static AudioDeviceInfo dev(int index, const std::string& name, int ins, int outs, const std::string& hwId = "") {
  AudioDeviceInfo d{};
  d.index = index;
  d.name = name;
  d.hostApi = "ALSA";
  d.maxInputs = ins;
  d.maxOutputs = outs;
  d.defaultSampleRate = 48000;
  d.hwId = hwId;
  return d;
}

int main() {
  // After a reboot the Ensoniq card came up as card 1 instead of card 0 (issue #68).
  std::vector<AudioDeviceInfo> v{
      dev(0, "HDA Intel PCH: ALC892 Analog (hw:0,0)", 2, 2),
      dev(1, "Ensoniq AudioPCI: ES1371 DAC2/ADC (hw:1,0)", 2, 2),
      dev(2, "Ensoniq AudioPCI: ES1371 DAC1 (hw:1,1)", 0, 2),
      dev(3, "pipewire", 64, 64),
  };
  auto found = [&](const std::string& name, bool input) {
    const AudioDeviceInfo* d = findAudioDevice(v, name, input);
    return d ? d->name : std::string("-");
  };
  CHECK_EQ(found("Ensoniq AudioPCI: ES1371 DAC2/ADC (hw:0,0)", true), v[1].name);
  CHECK_EQ(found("Ensoniq AudioPCI: ES1371 DAC2/ADC (hw:0,0)", false), v[1].name);
  // The device number still has to match: DAC1 is a different PCM on the same card.
  CHECK_EQ(found("Ensoniq AudioPCI: ES1371 DAC1 (hw:0,1)", false), v[2].name);
  CHECK_EQ(found("Ensoniq AudioPCI: ES1371 DAC1 (hw:0,0)", false), std::string("-"));
  // An exact name wins, and an output-only device is no input.
  CHECK_EQ(found("HDA Intel PCH: ALC892 Analog (hw:0,0)", true), v[0].name);
  CHECK_EQ(found("Ensoniq AudioPCI: ES1371 DAC1 (hw:1,1)", true), std::string("-"));
  CHECK_EQ(found("pipewire", true), std::string("pipewire"));
  // Unplugged, or never there.
  CHECK_EQ(found("USB Audio Device: USB Audio (hw:2,0)", true), std::string("-"));
  CHECK_EQ(found("", true), std::string("-"));
  CHECK_EQ(found("ALC892 Analog", true), std::string("-"));

  // The sound server's devices: found by their id, which doesn't change with card numbers, and
  // standing in for a hw: device the server has open (PortAudio then can't list it).
  AudioDeviceInfo mic = dev(3, std::string(kServerPrefix) + "alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback", 1, 0);
  mic.serverNode = "alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback";
  mic.serverCardName = "USB Audio Device";
  mic.serverPcm = 0;
  v.push_back(mic);
  CHECK_EQ(found(mic.name, true), mic.name);
  CHECK_EQ(found(mic.name, false), std::string("-"));
  CHECK_EQ(found("USB Audio Device: USB Audio (hw:2,0)", true), mic.name);
  CHECK_EQ(found("USB Audio Device: USB Audio (hw:2,1)", true), std::string("-"));
  CHECK_EQ(found("Other Card: USB Audio (hw:2,0)", true), std::string("-"));

  // Without a sound server: a USB card's own description and kind, else PortAudio's name.
  AudioDeviceInfo usb = dev(4, "USB Audio Device: USB Audio (hw:2,0)", 1, 2, "hw:2,0");
  usb.card = "C-Media Electronics Inc. USB Audio Device";
  usb.inputKind = "Mic";
  usb.outputKind = "Speaker";
  CHECK_EQ(usb.label(true), std::string("C-Media Electronics Inc. USB Audio Device — Mic (hw:2,0)"));
  CHECK_EQ(usb.label(false), std::string("C-Media Electronics Inc. USB Audio Device — Speaker (hw:2,0)"));
  CHECK_EQ(v[3].label(true), std::string("pipewire"));
  // A server device has no hw: address to show.
  v[4].serverInputName = "USB Audio Device Mono";
  v[4].serverInputPort = "Microphone";
  v[4].serverVendor = "C-Media Electronics, Inc.";
  CHECK_EQ(v[4].label(true), std::string("USB Audio Device Mono — Microphone (C-Media Electronics, Inc.)"));
  v[4].serverVendor.clear();
  CHECK_EQ(v[4].label(true), std::string("USB Audio Device Mono — Microphone"));
  v[4].serverUnplugged = true;  // a jack with nothing in it
  CHECK_EQ(v[4].label(true), std::string("USB Audio Device Mono — Microphone (unplugged)"));
  CHECK_EQ(v[0].label(true), v[0].name);

  // With one: its name, port and the card's maker, whatever the bus.
  usb.serverInputName = "USB Audio Device Mono";
  usb.serverInputPort = "Microphone";
  usb.serverOutputName = "USB Audio Device Analog Stereo";
  usb.serverOutputPort = "Speakers";
  usb.serverVendor = "C-Media Electronics, Inc.";
  CHECK_EQ(usb.label(true), std::string("USB Audio Device Mono — Microphone (C-Media Electronics, Inc., hw:2,0)"));
  CHECK_EQ(usb.label(false), std::string("USB Audio Device Analog Stereo — Speakers (C-Media Electronics, Inc., hw:2,0)"));
  // A name set in WirePlumber is used as it is.
  usb.serverInputName = "FT-710 RX";
  CHECK_EQ(usb.label(true), std::string("FT-710 RX — Microphone (C-Media Electronics, Inc., hw:2,0)"));
  // No port from the server: what ALSA says the input is.
  usb.serverInputPort.clear();
  CHECK_EQ(usb.label(true), std::string("FT-710 RX — Mic (C-Media Electronics, Inc., hw:2,0)"));
  // Parts the name already says aren't repeated; a card with no known maker has none.
  AudioDeviceInfo builtIn = dev(5, "X1E80100: - (hw:0,3)", 2, 0, "hw:0,3");
  builtIn.serverInputName = "Built-in Audio Internal microphones";
  builtIn.serverInputPort = "Internal microphones";
  CHECK_EQ(builtIn.label(true), std::string("Built-in Audio Internal microphones (hw:0,3)"));
  builtIn.serverVendor = "Intel Corporation";
  builtIn.serverInputName = "Intel Corporation microphones";
  CHECK_EQ(builtIn.label(true), std::string("Intel Corporation microphones — Internal microphones (hw:0,3)"));

  TEST_MAIN_END();
}
