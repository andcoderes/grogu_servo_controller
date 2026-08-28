# Bottango setup — 3D model for `grogu.btngo`

`grogu.btngo` is the Bottango Studio project for this droid. It rigs its
servos against a 3D model of the physical body so you can puppeteer and
author animations visually. The model files themselves are **not** in this
repo (they're large binaries, and gitignored under `models/`) — you supply
them once, locally.

## Where the model comes from

The body is **Project Gogurt** by jaigeyehunter, published on Patreon:

<https://www.patreon.com/jaigeyehunter/posts/project-gogurt-159288412>

Download the model pack from that post. The parts are distributed as
**STL** files (standard for 3D printing).

## STL → OBJ

Bottango Studio imports **`.obj`** or **`.fbx`** meshes — it does **not**
read `.stl`. So every part you want visible in the rig has to be converted
to OBJ first. Any of these work:

- **Blender** — `File > Import > STL`, then `File > Export > Wavefront
  (.obj)`. Batch-convert with a short script if there are many parts.
- **MeshLab** — open the STL, `File > Export Mesh As... > *.obj`.
- Any offline STL-to-OBJ converter (avoid uploading unreleased models to
  web converters).

Keep scale and orientation as-is on export; the rig in `grogu.btngo` was
built against the parts at their native STL scale.

## Where the files go

`grogu.btngo` references its models by **relative path**, as
`models\<name>.obj` (relative to the project file). Create a `models/`
folder next to `grogu.btngo` and put the converted OBJ files there, using
exactly these names:

```
models/
  body_base.obj
  Split Wrist Joint - Upper.obj
  Servo Bicep V2.obj
  Head Plug For Sonic Mount.obj
  Head Tilt Servo Mount.obj
  Middle Servo Plate.obj
```

Matching `.mtl` files can sit alongside them (optional — they only carry
material colour). `models/` is in `.gitignore`, so these stay local and
never get committed.

## Opening the project

Open `grogu.btngo` in Bottango Studio. If the model files are in
`models/` with the names above, the rig loads with its meshes attached. If
Bottango can't find a mesh (wrong name, wrong folder, or it resolves the
path from a different working directory), it will prompt you to relink —
point it at the matching file in `models/` once and re-save.
