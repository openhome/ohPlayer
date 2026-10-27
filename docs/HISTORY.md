# History

* **2026-10-27** — Radio Browser media server (radio-browser.info integration).
  * New DLNA Media Server device exposing the [radio-browser.info](https://www.radio-browser.info) community radio directory (45,000+ stations) to any UPnP control point. Browsable as `Radio Browser <room>` alongside MinimServer etc.
  * Navigation: Most popular, By country (short configurable list), By genre, By name (A-Z, a search-by-navigation workaround for control points that do not send ContentDirectory Search requests), each station list split into Hi-Res (>=192 kbps) / Mid-Res (96-191) / Low-Res (<96) sub-containers, alphabetically sorted.
  * Stations play on the renderer through its usual protocols (icecast/AAC, HLS m3u8). Station artwork (favicon) is passed through.
  * Quality of life: stations known to be down (`lastcheckok=0`) are hidden; duplicate submissions of the same stream are deduplicated (normalised url comparison); lists are cached for 60s.
  * See [docs/RADIO.md](RADIO.md) for details.

* **2026-10-27** — Radio presets, advanced pipeline buffer tuning, radio pinning without TuneIn, build and web ui fixes.
  * Radio presets are now configurable in the web ui (Radio tab): 10 slots, each with a Name, a stream URL and an optional station logo (artwork) URL. Defaults: 6 Radio France stations (hifi AAC icecast) and 4 RNE Spanish national stations (HLS). Edits apply without a reboot.
  * Radio presets and arbitrary stream URLs can be pinned from control point apps without a TuneIn partner id (RadioPins/UrlPins registered unconditionally).
  * Advanced pipeline buffer parameters exposed in the web ui (Device tab, Advanced) in pipeline-native units: `Pipeline.StarvationRamperMinSize` (jiffies, default 5644800 = 100 ms) and `Pipeline.EncodedReservoirSize` (bytes, default 1572864 = the ohPipeline default). Useful to absorb network jitter on unstable links.
  * `fetch_deps.sh` also fetches the `Generated/` ohNet headers (Cp*/Dv*) from the openhome/ohNetGenerated GitHub tag matching the configured version.
  * Fixed a ConfigUi crash on open after a store reset: the web ui message pool was exhausted by the burst of field notifications (pool raised to 200 messages / 300 x 64-byte buffers).
  * Quiet debug logging: the `kPipeline` and `kMedia` modules (one line per HLS segment / pipeline transition) are disabled by default.

* **2026-10-23** — Volume routing with official DS semantics, mixer-less sound cards, mDNS crash fix, install and build fixes.
  * `Internal Volume Control` (`Volume.Enabled`) exposed in the web ui (Volume tab): `Yes`/unset — internal ALSA volume; `No` with a serial port configured — external Linn amplifier over RS232; card without a mixer or amplifier not detected — volume greyed out in control points. The `LinnAmp.VolumeControl` parameter was removed.
  * New `LinnAmp.AlsaVolume` parameter (0-100, default 100) to level-match rooms when the Linn amplifier handles volume.
  * `StoreWithOverriddenValue` decouples the user's persisted intent from what the MediaPlayer sees at runtime.
  * Sound cards without a mixer are fully supported (no crash, no error spam).
  * mDNS host name is now derived from the Linux host name (`ohp-<hostname>`), fixing the sporadic `-65548` name-conflict crash between players.
  * Makefile: `install` target fixed (web ui resources to `/usr/share/openhome-player/res/`, `webconfig.js`, `lang/`), incremental `VersionInfo.h` generation, per-object dependency tracking (`-MMD -MP`).

* **2026-10-18** — Added external Linn Roomamp 2 support.
* **2026-09-04** — Synced back with AndreHro updates. Currently only used on `aarch64`, but it can easily be adapted to `armhf` if needed. Please raise an issue if support is required.

* **2025-04-19** — As `ohPlayer` is no longer maintained or aligned with the rest of the stack, the project was redesigned based on `ohPipeline/TestMediaPlayer`.

  Some build details can be found here:
  [ohContainerBuilder](https://github.com/jchassin/ohContainerBuilder)

  It can be built in an Ubuntu container or directly on a Raspberry Pi. Open a ticket if support is needed.

* **2021-10-16** — Added support for the Audiophonics Raspdac Mini LCD:
  [Audiophonics Raspdac Mini LCD Kit](https://www.audiophonics.fr/fr/lecteurs-reseau-open-source/audiophonics-raspdac-mini-lcd-kit-diy-lecteur-reseau-pour-raspberry-pi-4-dac-es9038q2m-p-15148.html)

  Initial fork added support for the Audiophonics Raspdac Mini OLED screen.

Based on the [OpenHome Sample Players](https://github.com/openhome/ohPlayer) for macOS, Windows, Linux and Raspberry Pi.
