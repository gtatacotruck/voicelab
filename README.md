<div align="center">

# VLAB

**A free, open source voice changer and soundboard for Windows.**

Live voice effects, autotune, a hotkey soundboard with routing, text to speech, a MIDI synth and a fully themeable UI, with a mod SDK if you want to build on top of it.

![Platform](https://img.shields.io/badge/platform-Windows-0b0b0b?style=flat-square&labelColor=241032)
![Version](https://img.shields.io/badge/version-1.9.6-00cc00?style=flat-square&labelColor=241032)
![License](https://img.shields.io/badge/license-MIT-ff8c1a?style=flat-square&labelColor=241032)
![Built with](https://img.shields.io/badge/built%20with-C%2B%2B%20%2F%20JUCE-5b2a86?style=flat-square&labelColor=241032)

[Website](https://veelab.vercel.app) &nbsp;|&nbsp; [Download](https://file.garden/aoIOyWCDZ1SJNkG_/Vlab1.9.6.zip) &nbsp;|&nbsp; [Telegram](https://t.me/vlabsoundboard) &nbsp;|&nbsp; [Mod shop](https://veelab.vercel.app/store.html)

</div>

---

> [!WARNING]
> **Official sites only.** The only official VLAB sites are **[veelab.vercel.app](https://veelab.vercel.app)** and **[vlab-two.vercel.app](https://vlab-two.vercel.app)**. Any other site claiming to be VLAB is fake and cannot be trusted.
>
> Someone has cloned the website and shipped a copy of VLAB with a RAT (remote access trojan) inside it. See [Staying safe](#staying-safe) below.

---

## Screenshots

<table>
  <tr>
    <td><img src="https://file.garden/aoIOyWCDZ1SJNkG_/tacotruckss1.png" alt="VLAB screenshot 1"></td>
    <td><img src="https://file.garden/aoIOyWCDZ1SJNkG_/tacotruckss2.png" alt="VLAB screenshot 2"></td>
  </tr>
  <tr>
    <td><img src="https://file.garden/aoIOyWCDZ1SJNkG_/tacotruckss3.png" alt="VLAB screenshot 3"></td>
    <td><img src="https://file.garden/aoIOyWCDZ1SJNkG_/tacotruckss4.png" alt="VLAB screenshot 4"></td>
  </tr>
</table>

## Quick start

1. Download the latest build from one of the [official sites](#official-sites).
2. Unzip it.
3. Run the exe. There is nothing to install.

VLAB is **Windows only**. There are no plans for a Linux version.

## Features

| | |
|---|---|
| **FX chain** | Pitch shifter, EQ, compressor, noise gate, distortion, bitcrush, tremolo, stutter, ring mod, chorus, phaser, flanger, auto wah, delay, reverb, pan and headspace, and a limiter so nobody gets their ears blown out. All of it runs live on your actual voice. |
| **Autotune** | Pick a key and scale, then set how fast it snaps. Retune speed 0 gives the hard, stepped autotune sound, higher values keep your natural vibrato. A live readout shows the note you are singing and the note it lands on. |
| **Soundboard** | Bind a hotkey to each slot, double tap to unbind, record straight into a slot, and keep multiple soundboards you can switch between. |
| **Soundboard routes** | Route 1 plays the soundboard after your voice effects, so they don't touch it. Route 2 sends it through your voice effects. You can also make custom routes that join the chain at any point. |
| **Text to speech** | Has its own tab. Pick a voice, adjust speed, volume and pitch, and watch the words highlight as it reads. |
| **MIDI and synth** | On screen piano with macros, plus an arpeggiator that handles chords. |
| **System echo** | Listen back to what other apps on your PC are playing, right from VLAB. |
| **Visualizers** | Waveform, spectrum analyzer, radial spectrum, VU meter, goniometer and a virtual head widget. |
| **Themeable UI** | Glass toggle, RGB cycling (rainbow, pulse, strobe, chase), a theme creator, and UI presets that save and load your whole layout. |
| **Multi language** | The UI is not stuck in English. Language mods can translate any text in the app, and there is a built in button to export a template with every string. |
| **Scroll settings** | The mouse wheel can scroll the page everywhere, including over the background. A setting controls whether it also moves sliders and drop-downs. |
| **Discord RPC** | Show what you are up to in Discord. |
| **Mod SDK** | Write your own DLL audio mods, JSON theme mods, language mods, or app mods that add visual addons on top of audio mods. |

There is also a small pong minigame hidden in there if you poke around enough.

## Mods

VLAB has four kinds of mods:

- **Audio mods**: DLLs that plug into the audio chain.
- **Theme mods**: JSON files that change how the UI looks.
- **Language mods**: JSON files that translate the UI. Use **Settings > General > Export Language Template** to get a file listing every string.
- **App mods**: visual addons that sit on top of audio mods.

Community mods live in the [mod shop](https://veelab.vercel.app/store.html). To make your own, download VLAB or read the code here on GitHub, and the SDK documentation is included.

## Changelog

**1.9.6**

- Fixed: the FX rack shrinking when sections were collapsed and expanded (new UI).
- Fixed: scrolling now works with the cursor on the background.
- Fixed: the soundboard's send reverb no longer shares state with the master reverb.
- Added: setting to turn the slider and drop-down mouse wheel on or off (off by default).
- Added: language mods can translate any text, plus Export Language Template.
- Added: Autotune in the FX Rack.
- Added: soundboard routes (Route 1, Route 2, custom) on the Board tab.
- Removed: the jellyfish overlay and its Ctrl+Shift+J shortcut.

The full changelog is on the [website](https://veelab.vercel.app/#changelog).

## Official sites

| Site | Link |
|---|---|
| Main | [veelab.vercel.app](https://veelab.vercel.app) |
| Backup | [vlab-two.vercel.app](https://vlab-two.vercel.app) |

Nothing else is official.

## Staying safe

Some people took VLAB, put a RAT inside it, and cloned the website so the fake looks real. To stay safe:

- Only download VLAB from the two official sites above, and check the address bar before you click download.
- If you got VLAB from anywhere else, delete it and do not run it.
- VLAB will never ask for your passwords or tokens. If something claiming to be VLAB does, close it.
- If you already ran a fake copy, disconnect from the internet, run a full antivirus scan, then change your passwords from a different, clean device and turn on 2FA.
- If you find another clone, tell us on [Telegram](https://t.me/vlabsoundboard) so it can be reported.

## The name

VLAB started life as a search for a name that had something to do with voice and mods. Voicemod was already taken, and at the time the developer was playing a lot of BONELAB, so the two ideas got merged into **VoiceLab**, which was shortened to **VLAB**.

It is pronounced **vee-lab**, not "vuh-lah-buh", though plenty of people say it that way. In some parts of the code it is also called **Vanadium**.

## History

- **The python script.** VLAB began as a small python soundboard and voice changer. It only kept going because someone asked for the original script and liked it, so it kept getting updates. That original script is lost now.
- **VLAB Lite.** Python turned out to be too limiting and too slow, so everything was rebuilt in C++ with JUCE, with a real DSP chain.
- **VLAB.** Lite has since grown into the app you see today, and it is used daily as the developer's main mic program. A lot of it gets written at night.

A fun fact from the python days: the variable that held the entire DSP was named `poop`, as a joke, and it went on for too long to rename.

VLAB used to be entirely in French too, until the developer forgot how to speak it and switched everything to English.

## FAQ

<details>
<summary><b>How long will VLAB be supported?</b></summary>

Development continues as long as there are ideas to add, improve or fix.
</details>

<details>
<summary><b>Why doesn't VLAB have feature X like program Y?</b></summary>

It is built by one person, not a company with a team. Some features take a long time to make, so please be patient.
</details>

<details>
<summary><b>Has VLAB Lite support ended?</b></summary>

Mostly. It will still get bug fixes and important maintenance updates, but do not expect major new features there.
</details>

<details>
<summary><b>Will VLAB ever support Linux?</b></summary>

No.
</details>

<details>
<summary><b>Is VLAB supposed to compete with Voicemod?</b></summary>

Not really. VLAB was made to work the way its developer wanted it to.
</details>

<details>
<summary><b>Do you take suggestions?</b></summary>

Yes, and most of them get read. Keep them realistic and explain what you actually want.
</details>

<details>
<summary><b>Do you use AI to write VLAB?</b></summary>

A little. AI autocomplete gets used sometimes to speed up repetitive code. The project is roughly 10% AI, 20% the developer, and 80% YouTube tutorials.
</details>

<details>
<summary><b>Why was VLAB made?</b></summary>

- Voicemod locks too many features behind a paywall.
- Equalizer APO was not a good fit.
- FL Studio was not an option for what was wanted.
- It was heavily inspired by a small soundboard called Resonance, which is worth downloading.
</details>

<details>
<summary><b>Is VLAB open source?</b></summary>

Yes. The code is out there to read, modify and learn from, under the MIT license.
</details>

<details>
<summary><b>Can I make mods for VLAB?</b></summary>

Absolutely. Download VLAB or read the code here on GitHub, and the SDK is included.
</details>

<details>
<summary><b>Will VLAB ever become paid?</b></summary>

Probably not. The plan is to keep it free, though there is no promise it will never change. Please do not send donations, they will not be accepted.
</details>

## Credits

- **[gtatacotruck](https://github.com/gtatacotruck)** started VLAB and maintains it.
- **A friend** helped build the very first version.
- **Soundux**: a lot of VLAB is based on it.
- **Resonance**: a small soundboard that inspired VLAB.
- Everyone who has made mods and themes, reported bugs, and hung around the Telegram.

## Contact

- **Telegram group**: [t.me/vlabsoundboard](https://t.me/vlabsoundboard), for bug reports, showing off mods, and chatting with other users.
- **Telegram (direct)**: [t.me/Vlabofficial](https://t.me/Vlabofficial)
- **GitHub**: [github.com/gtatacotruck](https://github.com/gtatacotruck)

## License

Released under the [MIT License](LICENSE).
