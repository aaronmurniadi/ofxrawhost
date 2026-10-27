# Noise profiles

`noiseprofiles.json` is detector noise data taken from the
[darktable](https://github.com/darktable-org/darktable) project, file
`data/noiseprofiles.json`, at commit `c3b114dfe79359c04e041eeb5fff69756082c92f`
(6 April 2026). It is unmodified: replacing the file with a newer one from
darktable is all an update needs.

The file holds, for every supported camera and ISO setting, the fit of the
sensor noise to

```
V(X) = a * X + b
```

with `a` and `b` per colour channel, measured on a flat shot by darktable
contributors. The host reads the green channel of the fit to seed the noise
model of the bundled Denoise (profiled) plugin: it matches the camera make and
model that LibRaw reports for the opened RAW file, then takes the profile of the
EXIF ISO setting, interpolating between the two neighbouring ISO profiles when
there is no exact entry.

## License

The file comes from darktable and stays under darktable's license, the GNU
General Public License version 3 or later; the full text is in `COPYING`
alongside it. It is data, not code: the host reads it at runtime. The rest of
OFX Raw Host is under the MIT license (see the repository `LICENSE`), and this
directory is the only place where GPL-licensed material is bundled.
