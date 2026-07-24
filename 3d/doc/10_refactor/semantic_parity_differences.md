# Deliberate Differences from the ShaderToy Reference

**Status:** Phase 8 milestone record
**Scope:** Documented, deliberate divergences between the native port and the
captured ShaderToy snapshot (`shader_toy/Common.glsl`, `CubeA.glsl`,
`Image.glsl`). Every entry is a conscious, recorded decision; none is a hidden
calibration constant. None alters a locked semantic invariant (G1–G10).

---

## 1. Reflective = zero in the parity kernel

**Reference:** mirror sphere/box are traced but contribute nothing to GI
(`CubeA.glsl:156-158` — "Reflective: Do nothing").

**Native decision:** identical to the reference. Mirror surfaces render black
in the parity kernel and are excluded from transport. A path tracer of the
same scene shows true mirror reflections, so PT-vs-RC deltas concentrate on
the mirror sphere/box — an expected, deliberate difference, not a port bug.

## 2. Chart-edge clamp for upper-merge look-back

**Reference:** `CubeA.glsl:35` uses the raw (unclamped) upper candidate
coordinate for the look-back distance fetch; only the radiance fetch clamps.
At chart edges the literal source can read into a neighboring chart/cascade.

**Native decision:** apply the plan's safer policy — candidate coordinates are
clamped to the owning chart before *both* look-back and radiance addressing.
This prevents cross-chart reads at edges while preserving the reference
semantics everywhere the reference is well-defined.

## 3. Two distinct PI literals preserved verbatim

**Reference:** theta uses `3.14192653` (a typo in the source) while azimuth
and weighting use `3.141592653`.

**Native decision:** preserve both literals exactly, unconsolidated. Cleaning
the typo up would be a semantic change and is forbidden; the fixtures lock
this.

## 4. No inverse-square attenuation

**Reference:** directional sun and emissive surfaces contribute without
distance falloff.

**Native decision:** identical (locked semantic). A physically correct area
light falls off with distance, so a small emissive source makes the native RC
brighter than a physically-falloff PT. This is a convention difference, not
an error.

## 5. Final display is declared native policy, not parity

**Reference:** the captured snapshot has no final compositor (`Image.glsl` is
byte-identical to `CubeA.glsl`).

**Native decision:** the final consumer is declared native display policy in
the `LightingView` schema — exact four-bin reconstruction, visible-surface
albedo applied, no `1/pi`, direct light composited separately, linear display
map. This policy cannot alter the G1–G8 reference atlas and is recorded, not
hidden.

## 6. Cascade-band offset is the band height (256), not chart height

**Reference:** merge uses `+ gRes.y` for the upper band, which equals 256 only
because all reference charts are 256 tall.

**Native decision:** use the cascade band height (256) explicitly. This is
equivalent for the parity scene and correct for additional scenes whose charts
are shorter (e.g., the legacy Cornell light/box-top charts are 64 tall). This
fixes a latent assumption, not a reference semantic change.

## 7. Legacy Cornell scene integration is additive, not a parity change

**Native decision:** the legacy Cornell box is supported as a *second* scene
with its own chart layout (1/128 texel scale) and trace path, for controlled
same-scene comparison. The locked ShaderToy parity scene, its layout kernel,
and all G0–G10 evidence are unchanged. The legacy scene's box side faces are
currently uncharted (documented integration limitation); box top faces are
charted.

---

**Rule:** any future change that would alter one of these differences must
first be documented here and must not invalidate a locked invariant. If it
would alter a locked invariant, progression is prohibited and the port is
corrected instead.
