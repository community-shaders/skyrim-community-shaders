# Aerosol controls

Physical Sky evaluates aerosol optical properties on the CPU. The atmosphere
shader, constant-buffer layout, Rayleigh coefficients, and ozone model are
unchanged. The three controls are aerosol type, particle loading, and relative
humidity. Custom retains direct editing of scattering, absorption, and phase.

## Data source

The tables derive from **Optical Properties of Aerosols and Clouds (OPAC) 4.0**,
by Peter Koepke, Josef Gasteiger, and Michael Hess:

- [Software and data, DOI 10.5281/zenodo.7275005](https://doi.org/10.5281/zenodo.7275005)
- [Hess, Koepke, and Schult (1998), original OPAC model](<https://doi.org/10.1175/1520-0477(1998)079%3C0831:OPOAAC%3E2.0.CO;2>)
- [Koepke, Gasteiger, and Hess (2015), nonspherical mineral particles](https://doi.org/10.5194/acp-15-5947-2015)

The published `opac_40b.zip` has MD5 `04163f8d40758e59744ae5fce1dd3253`
and SHA-256 `561d95f60e7151861f0114f7e654b85fb763ed367263ba76255780fc735d110d`.
The generator reads the optical data directly; it does not execute the OPAC
Fortran program. Reproduce the checked-in table with:

```powershell
python tools/generate-aerosol-data.py path/to/opac_40b.zip
python tools/generate-aerosol-data.py path/to/opac_40b.zip --check
```

The reference mixtures come from `opac40/opac_40.cfg`. Number densities are
particles per cubic centimetre at the model's reference ground level:

| Type                   | OPAC components and reference number densities |
| ---------------------- | ---------------------------------------------- |
| Continental background | INSO 0.15, WASO 2600                           |
| Marine                 | WASO 1500, SSAM 20, SSCM 0.0032                |
| Urban pollution        | INSO 1.5, WASO 28000, SOOT 130000              |
| Desert dust            | WASO 2000, MINN 269.5, MIAN 30.5, MICN 0.142   |

Particle loading multiplies every component's number density by the same factor
in [0, 10]. A value of 1 means the reference mixture, not an equal mass or equal
extinction across types. Loading is independent of water uptake and is neither
AQI nor PM2.5. Zero removes aerosol scattering and absorption.

## Reduction and humidity

The RGB representative wavelengths are 680, 550, and 440 nm, also used by the
solar limb-darkening model. This is a three-wavelength approximation, not a
spectral-to-sRGB colorimetric integration. At each wavelength the generator
linearly interpolates OPAC scattering and absorption coefficients and sums
them using the reference component number densities. OPAC coefficients are
per kilometre for one particle per cubic centimetre; multiplying by 1000
converts the mixture coefficients to the existing settings units, per megametre.

Humidity knots are 0, 50, 70, 80, 90, 95, 98, and 99 percent RH. Between knots,
the CPU interpolates scattering, absorption, and the green-channel scattering
first moment. This is interpolation of tabulated optical properties, not an
exact microphysical solution at intermediate humidities. Water-soluble and
sea-salt components use their humidity-dependent data; insoluble particles,
soot, and minerals use OPAC's humidity-independent tables. The model does not
add a generic humidity multiplier to absorbing soot.

Relative humidity is uniform throughout the effective aerosol layer. The
existing exponential altitude profile remains controlled by `aerosolFalloff`;
OPAC's multilayer altitude profiles are not imported. Condensation, cloud
activation, fog droplets, and humidity hysteresis are not simulated. RH is
limited to the available range [0, 99], with no extrapolation to saturation.

## Phase and weather interpolation

OPAC's asymmetry parameter is the mean scattering cosine. The shader uses a
Cornette-Shanks parameter `g`, whose mean cosine is:

```text
meanCosine(g) = 3 g (4 + g^2) / (5 (2 + g^2))
```

The generator stores `scattering(550 nm) * meanCosine(550 nm)` alongside the
RGB coefficients. The CPU recovers the mean cosine and inverts the equation
with a bounded bisection. Matching the first moment at 550 nm preserves the
shader's single, wavelength-independent phase approximation; it does not
reproduce the full OPAC phase function.

Weather and time-of-day transitions blend numeric settings addresses, so the
aerosol appearance travels across a transition as the three coefficients, not
as the mixture: a mixture is a discrete choice and has no midpoint to
interpolate to. The scene layer therefore stores the coefficients, and the
mixture, the loading and the relative humidity are authoring inputs that derive
them. `Settings::ApplyAerosolOptics()` writes that derivation into
`aerosolScatter`, `aerosolAbsorption` and `aerosolPhaseG` whenever an input
moves, and `SceneSettingsPolicy` bars the three inputs from the scene layer so
nothing can store a mixture as if it blended.

`PhysicalSky::GenerateLuts()` reads the three coefficients. A scene page draws
its own aerosol picker, which evaluates the chosen mixture and writes the
resulting coefficients into that context's own entries, so one page can hold a
different mixture from the next and a transition interpolates between them. The
loading and humidity a page was authored with are not stored with it; the
picker opens on the feature's own inputs and the coefficients below it are the
scene's stored state.

`Aerosol::Lerp` is the exact moment-preserving blend of two evaluated
endpoints, for a caller that holds both:

```text
scatter = lerp(scatterA, scatterB, t)
absorption = lerp(absorptionA, absorptionB, t)
moment = lerp(scatterA.green * meanCosineA,
              scatterB.green * meanCosineB, t)
meanCosine = moment / scatter.green
```

The phase is irrelevant when green scattering is zero, and endpoint values are
returned unchanged at t=0 and t=1. This models an external optical mixture at a
common altitude profile, with a single phase fitted to its first moment; the
helper blends optical coefficients only. The framework's per-address blend is
component-wise and agrees with it for scattering and absorption, but
interpolates the phase parameter linearly rather than through the first moment,
which differs only by the nonlinearity of the inversion above.

## Configuration compatibility

`aerosolType` is 0 for Custom, 1 for Continental, 2 for Marine, 3 for Urban,
and 4 for Desert. `aerosolLoading` defaults to 1 and `aerosolHumidity` to 50.
The default type remains Custom, so existing presets and restored defaults
retain their previous coefficients and appearance. Missing or unknown type
values use the custom coefficients. Selecting a physical type activates the
three controls and writes the coefficients it resolves to; switching back to
Custom keeps them, because a physical type had already written them, so the
appearance is continuous at that point.

A settings document that moves the type, the loading or the humidity re-derives
the coefficients as it loads, which is what keeps a preset or a feature
override authored in those terms meaningful. A document that moves only the
coefficients - the scene layer's own output - is taken as written. Hand-editing
the coefficients while a physical type is selected is therefore overwritten by
that type; select Custom first.

The physical evaluator clamps loading and RH and replaces nonfinite values
with their defaults. Its output feeds the existing per-game-unit conversion.
Custom coefficients are passed through unchanged for compatibility.
