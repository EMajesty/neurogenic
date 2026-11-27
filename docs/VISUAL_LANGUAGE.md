# Functional visual language

This document specifies semantic direction, not final artwork. Functional
color identifies meaning, brightness identifies emphasis, and animation
identifies activity. Operational state takes precedence. Color is never the
only indication of critical state, and literal RGB values never become musical
or DSP state.

| Functional family | Destinations | Provisional color |
|---|---|---|
| Performance | PERF / XY | Purple `#B58AEE` |
| Sequencing | SEQ / SONG | Blue `#548CE8` |
| Sample material | SAMPLE / SLICE | Orange `#E5A052` |
| Sound generation | SOUND / PRESET | Teal `#51BCB3` |
| Mixing and processing | MIX / FX | Green `#8ABF70` |
| Modulation | MOD / ROUTE | Magenta `#D47BB3` |
| Musical abstraction | MACRO / SCENE | Yellow `#D5CA69` |
| Library and system | BROWSE / SETUP | Neutral `#9BA7B8` |

Primary and secondary destinations inherit their family. The screen retains a
neutral dark base instead of replacing its entire background. An available
inactive page is dim family color; the active page is brighter. A secondary
destination retains the family but also shows an explicit SHIFT/subview label.
Unavailable destinations are explicitly inactive. Hue alone never
distinguishes primary from secondary.

Contextual controls inherit the category of their assigned operation, which is
especially important in Performance banks: cutoff is Sound, delay throw is
Mix/FX, pattern Fill is Sequencing, reverse is Sample, modulation depth is
Modulation, and scene recall is Macro/Scene. Screen labels and illuminated
physical controls project equivalent meaning; an unilluminated control still
has its label.

## Operational precedence

The proposed deterministic order, highest first, is:

1. recording/capture (red plus activity label/pattern);
2. queued/waiting (amber plus temporal indication);
3. selected/focused (bright or white plus focus/selection shape or label);
4. playing/enabled (green where appropriate plus icon/text/activity);
5. modifier/alternate layer (explicit SHIFT/layer indication, blue where
   appropriate);
6. parameter lock/special context (purple plus lock symbol/text);
7. muted (reduced emphasis plus mute label/icon);
8. functional family color;
9. unavailable/unassigned (off or explicit inactive presentation).

Because functional and operational hues overlap, brightness, pulse/blink,
labels and icons provide redundant cues. Critical states never rely on hue.

SHIFT atomically changes secondary page and function labels, secondary
Performance assignments, modifier-dependent pad roles, and applicable physical
illumination. Release restores the primary projection. Selection-only pads
must be visibly different from trigger/recall pads.

## Architecture

The intended pipeline is:

`semantic action or parameter -> functional category -> theme palette ->`
`contextual and operational state -> rendering policy -> Slint/controller`.

Functional category metadata may be shared. Literal colors, brightness and
animation belong to theme/controller rendering policy. Alternative frontends
may choose another palette or no color.

Future policy may offer the default palette, user palettes, monochrome and
color-vision-accessible modes, reduced-brightness performance mode, and
controller-specific rendering. None is implemented by Task 098A.

Semantic color, RGB instruction, brightness, animation, firmware electrical
limits and physical LED placement remain separate. One logical control may
drive several addressable LEDs. Diffusion, current limits and readability need
physical validation; no LED count or board limit is assumed here.

## Illuminated contextual encoders

For an assigned contextual encoder, hue communicates functional identity and
brightness communicates normalized parameter magnitude. Operational feedback
may temporarily override either dimension; the screen remains the detailed
numerical authority. Minimum-valued assignments retain a visible minimum
brightness, while unassigned or unavailable encoders may be dark.

- Unipolar values map minimum to maximum brightness.
- Bipolar values are dim at neutral and brighter toward either extreme;
  direction remains explicit on screen.
- Logarithmic values follow normalized perceptual position.
- Boolean values are dim off and bright on.
- Enumerations use constant brightness unless meaningful ordering is declared.

Future resolved values may represent a base parameter, captured held-step
lock, secondary Performance assignment, mixed multi-selection, pickup state,
optional effective automation, or optional modulation visualization.
Continuous modulation visualization is not mandatory and live feedback rates
remain bounded. Minimum brightness, gamma, transitions, global illumination,
and firmware electrical limits are separate policies pending physical tests.

Protocol 1.4 implements the host side for negotiated contextual encoder RGB.
The host applies descriptor normalization, functional/operational color and the
31/255 assigned minimum exactly once, then sends final perceptual values via
LED_RENDER. Firmware owns calibration, gamma, global brightness, electrical
limiting and physical mapping; it must not apply another semantic minimum or
recolor. Baseline sessions receive no encoder destinations. Physical appearance
and electrical behavior remain NOT RUN.

## Typography direction

Walkway is a candidate typeface being evaluated by the project owner. It is
not bundled and no build depends on a local font directory. The direction is a
neutral dark foundation, functional accents, legible labels at 1920x1200 and
1024x600, and optionally a separate numeric or monospaced face. Redistribution
and licensing require verification before appliance inclusion. Typography and
artwork remain provisional.
