# Architecture

Fast Vending Kiosk's entire UI — header, categories, product grid, cart
strip, checkout overlay — is drawn by hand-written C++ using EGL/OpenGL ES
2.0. There is no `ConstraintLayout`, no `RecyclerView`, no Jetpack Compose,
no XML layout files. Kotlin exists only to satisfy things only Kotlin can do:
Activity lifecycle callbacks, a `Surface` to hand to EGL, and HTTPS/bitmap
decoding via the Android platform APIs.

This document explains what's actually happening at runtime and, more
importantly, why that trade-off was made deliberately for *this* app rather
than being native-code-for-its-own-sake.

## What's actually running

```
Kotlin (UI thread)                 Render thread (native, one per process)
───────────────────                ──────────────────────────────────────
MainActivity.onCreate                NativeApp::init()
  → nativeInit()                       spawns the thread, nothing to draw yet
  → nativeAttachCallbackTarget()

KioskSurfaceView                     CommandQueue (mutex + condvar)
  surfaceCreated/Changed/Destroyed  ←──┐  blocking wait, 1000ms timeout
  onTouchEvent (down/move/up)       ───┤  (16ms while a fling is decaying)
                                        │
NetworkClient (background threads)     ▼
  HttpURLConnection → JSON    ────→  drain queued commands
  BitmapFactory → RGBA bytes  ────→  mutate ProductRepository / CartManager /
                                      ImageManager / scroll offsets
                                      recompute LayoutEngine
                                      issue new network requests if needed
                                      (edge-triggered: only for newly-visible
                                      products, never every frame)
                                        │
                                        ▼
                                      Renderer::draw()
                                        QuadBatch (one shared shader,
                                        batched by texture/clip)
                                        → eglSwapBuffers
```

Everything left of the arrow only ever *posts a command* and returns
immediately. Everything right of the arrow — business state, layout, and all
GL calls — happens exclusively on one thread. No locks are held while a
frame is being drawn; commands are drained into a local vector first.

Rendering is **on-demand**: the render thread blocks on the queue until
something actually changes (a touch, a network result, a fling tick). On the
test device, zero frames are logged during 8 seconds of no interaction.
There's no `postInvalidate()`-driven choreographer loop ticking at 60fps for
a static screen.

### The pieces

| Component | Role |
|---|---|
| `core/json.*`, `core/product_repository.*`, `core/cart_manager.*` | Platform-independent business logic — no Android/GL/JNI dependency, host-compilable, unit-tested without a device (`tests/`) |
| `gl/renderer.*` | Owns the EGL context/surface; the only place that calls `eglMakeCurrent`/`eglSwapBuffers` |
| `gl/quad_batch.*` | One vertex shader + one fragment shader for the *entire app*. Solid rects, product thumbnails, and text all go through it; draw calls are only issued when the bound texture or clip region changes |
| `gl/font_atlas.*` | `stb_truetype`-baked glyph atlas (4 sizes), baked once at startup, never rasterized per-frame |
| `layout/layout_engine.*` | Pure function: screen size + density + scroll state → every rect on screen. No Android View measure/layout passes involved |
| `image/image_manager.*` | Bounded LRU for decoded pixels (200) and GPU textures (60), survives EGL context loss without re-fetching |
| `input/input_controller.*` | Hit-testing + drag/fling velocity tracking against the layout produced above |

## Why this instead of Kotlin + XML (or Compose)

This was built as an explicit architecture experiment: prototype on a phone,
then carry the *same binary architecture* to an RK3568 kiosk board with the
same 4GB of RAM. That goal shapes every answer below — these are not general
claims that native rendering beats the standard Android UI toolkit, they're
reasons it fits *this* constraint.

**1. One draw loop instead of a View tree.**
A `RecyclerView` grid of 100+ product cards means 100+ live `ViewGroup`
subtrees, each running its own measure → layout → draw pass, each capable of
triggering `invalidate()`/`requestLayout()` independently. Here, a frame is:
walk a flat array, push quads into one batch, flush on texture/clip changes,
one `eglSwapBuffers`. Typically well under a dozen actual GL draw calls for
the whole screen, regardless of how many products are in the catalog,
because off-screen cards are skipped before they ever reach the renderer
(`layout_.grid` intersection test), not measured-then-clipped.

**2. Memory is a number you chose, not a number you hope for.**
`ImageManager` caps decoded thumbnails at 200 and GPU textures at 60,
explicitly, in code you can point at. The equivalent in a View-based app —
`RecyclerView` view recycling plus an image-loading library's (Glide/Coil)
internal memory and disk cache — is tuned by that library's heuristics,
which are generally built for phones with far more headroom than a 4GB
embedded kiosk board. On this project, verified on-device: ~2MB of decoded
image cache, ~20–40 live textures, no growth over repeated scrolling and
refresh cycles.

**3. Frame cost is predictable across very different GPUs.**
A Snapdragon phone and an RK3568's embedded Mali GPU behave very
differently under the ART runtime + Android widget toolkit + OEM skin
overhead (MIUI, in this case) — recomposition cost, overdraw from themed
drawables, binder traffic for window insets, etc. all vary in ways that are
hard to separate from "the UI toolkit" when benchmarking. A raw EGL/GLES
loop strips that down to: how fast can this GPU push these specific batched
draw calls. That's the number this experiment actually wanted to compare
across hardware.

**4. No dependency surface to carry to a kiosk board.**
No RecyclerView, no Glide/Coil, no Material Components, no Compose runtime.
`app/build.gradle.kts` depends on nothing but `androidx.core.ktx`. On a
locked-down embedded Android image (which is what an RK3568 kiosk build
usually is), fewer Jetpack/Play-Services-adjacent dependencies means fewer
things that can be missing, version-mismatched, or behave differently from
the phone you prototyped on.

**5. The renderer is still fully dynamic, not a fixed mock-up.**
`LayoutEngine::recompute()` is a pure function of screen width/height/
density — it's what made portrait support (see git history) a one-line
manifest change with zero renderer code touched, and what makes the grid
column count respond to the actual viewport instead of being hand-tuned per
device. That dynamism is usually Compose/ConstraintLayout's selling point
over "just draw it yourself" — here it's implemented directly instead of
rented from the framework.

## What this costs — the honest trade-offs

This is *not* a free win, and for the overwhelming majority of Android apps
the standard toolkit is the right call. Specifically given up here:

- **Accessibility.** TalkBack, focus navigation, and Espresso/UI Automator
  all operate on the View hierarchy. There isn't one. None of that exists
  for this screen today.
- **Text shaping.** `stb_truetype` lays out left-to-right ASCII/Latin-1
  glyphs from a fixed atlas. Android's text stack gives you bidi, complex
  scripts, emoji, and font fallback for free; none of that is here.
- **Tooling.** No Layout Inspector, no live XML preview, no Compose
  previews. Changing a pixel offset means editing `layout_engine.cpp` and
  rebuilding.
- **Development speed.** This is ~3,200 lines of C++ to reproduce what a
  `RecyclerView` + `ConstraintLayout` + Glide would give you in a few
  hundred lines of Kotlin/XML. Every interaction (tap, drag, fling) was
  hand-built in `InputController` instead of inherited from `View`'s touch
  dispatch, `OverScroller`, and `GestureDetector`.
- **Platform integration.** Standard widgets, ripple effects, system
  theming (light/dark, dynamic color), window insets handling, predictive
  back — all absent unless explicitly rebuilt.

The honest framing: this architecture earns its cost specifically when the
screen is small, fixed, and performance/memory-critical on constrained
hardware you don't fully control (a kiosk), and the product surface doesn't
need accessibility/i18n/platform-integration depth. For a typical app
screen, Kotlin + Compose (or XML) is almost always the better engineering
decision — faster to build, easier to maintain, and you get the toolkit's
decade of accessibility/i18n/tooling investment for free.
