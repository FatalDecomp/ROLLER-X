#!/usr/bin/env python3
"""Turn one arena machine into a rigged, posable .blend.

The machines have no model files -- each is assembled from boxes and frusta
by ``mecha_mesh.c`` every frame -- so this builds the compiled game's own
geometry and skeleton and writes them out as something a modelling package
can open.

It runs in two halves.  ``tools/mech_export.c`` links against the game's mesh
module, builds the machine standing still, and prints its polygons, its
joints, and for every polygon the joint that posed it.  This script welds
that soup of quads into a mesh, raises an armature on the joints, and skins
the one to the other.

The weights are rigid -- every vertex belongs wholly to one bone -- and that
is not a shortcut.  These machines are plate armour: a shin plate is welded
to the shin and should not stretch when the knee bends.  The builder's own
frames are the truth about which plate is on which bone, which is why the
tag exists in the first place [MESHH-05].

Usage::

    python3 tools/export_mech_blend.py                     # Lilia 07
    python3 tools/export_mech_blend.py "SJ Mk.IV" -o sj.blend

Needs the ``bpy`` module (``pip install bpy``) and a C compiler.  Writes no
retail data: the colours are the project's own fallback palette.

**The .blend is written in the format of whichever bpy is installed**, and
Blender does not open files from a later version than itself.  To produce a
file for an older Blender, install the matching module in a virtualenv and
run this script with it -- ``pip install "bpy==4.3.0" "numpy<2"`` gives a 4.3
file.  The script itself is kept working across 4.x and 5.x.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SOURCES = [
    "PROJECTS/ROLLER/mecha_math.c",
    "PROJECTS/ROLLER/mecha_arena.c",
    "PROJECTS/ROLLER/mecha_defs.c",
    "PROJECTS/ROLLER/mecha_sim.c",
    "PROJECTS/ROLLER/mecha_ai.c",
    "PROJECTS/ROLLER/mecha_mesh.c",
    "PROJECTS/ROLLER/carplans.c",
    "tools/mech_export.c",
]

# Two vertices this close together, on the same bone, are the same vertex.
# The builder's primitives share corners exactly, so this only has to absorb
# the last bit or two of float noise.
WELD_EPSILON = 1e-4

# A bone Blender will accept: anything shorter than this is a bone it drops.
MIN_BONE_LENGTH = 0.01


def dump_machine(name, keep=None):
    """Build the exporter and run it, returning the parsed machine."""
    cc = os.environ.get("CC", "gcc")
    if shutil.which(cc) is None:
        sys.exit("no C compiler: set CC, or install %s" % cc)
    out_dir = keep or tempfile.mkdtemp(prefix="mech-export-")
    binary = os.path.join(out_dir, "mech_export")
    cmd = [cc, "-std=c99", "-O1", "-I", os.path.join(ROOT, "PROJECTS/ROLLER")]
    cmd += [os.path.join(ROOT, s) for s in SOURCES]
    cmd += ["-lm", "-o", binary]
    subprocess.run(cmd, check=True, cwd=ROOT)
    done = subprocess.run([binary, name], check=True, cwd=ROOT,
                          stdout=subprocess.PIPE)
    return json.loads(done.stdout)


def to_blender(v, scale):
    """Game space to Blender space.

    The game is Y-up, +Z the way a machine faces, +X to the right of a camera
    looking along +Z -- and that basis is **left**-handed, which is the whole
    difficulty here.  ``mecha_camera_basis`` builds right=+X, up=+Y,
    forward=+Z, and right x up = forward is the left-handed identity; a
    right-handed camera looks down its own -Z.  It also means a machine's own
    left is at -X, which is what the builder's ``iSide`` 0 and the tests both
    assume.

    Blender is right-handed and Z-up, and looks along +Y in front view, so a
    character faces -Y.  Getting there needs an axis swap **and** a flip, or
    the mech arrives mirrored: left arm on the right, and every asymmetry on
    the machine reversed with it.  Mapping (x, y, z) to (-x, -z, y) does
    both -- its determinant is -1, which is the flip, and it lands the
    machine's left at Blender's +X, which is a character's left when it faces
    -Y.
    """
    return (-v[0] * scale, -v[2] * scale, v[1] * scale)


def build_mesh(machine, scale):
    """Weld the quads per bone.  Returns verts, faces, face bone, face palette."""
    verts = []
    faces = []
    face_bone = []
    face_palette = []
    index = {}

    for quad in machine["quads"]:
        bone = quad["bone"]
        corners = []
        for v in quad["v"]:
            p = to_blender(v, scale)
            key = (bone,
                   round(p[0] / WELD_EPSILON),
                   round(p[1] / WELD_EPSILON),
                   round(p[2] / WELD_EPSILON))
            if key not in index:
                index[key] = len(verts)
                verts.append(p)
            corners.append(index[key])
        # A frustum that tapers to a point emits quads with a doubled corner.
        # Blender will not take a face with a repeated vertex, so those come
        # through as triangles, which is what they already are.
        unique = list(dict.fromkeys(corners))
        if len(unique) < 3:
            continue
        faces.append(tuple(unique))
        face_bone.append(bone)
        face_palette.append(quad["palette"])
    return verts, faces, face_bone, face_palette


def bone_tails(machine, bones, verts_by_bone, scale, stub):
    """Where each bone points.

    Blender bones run head to tail and have to have a length, which the
    game's frames do not: a frame is a point and three axes, and two of them
    (a shoulder and the upper arm hanging off it) sit on exactly the same
    point.  So a tail is chosen, in this order: at the children if there are
    any and they are somewhere else, down the bone's own geometry if not,
    and failing both down the frame's own -Y, which is the way every limb on
    these machines hangs.
    """
    heads = {b["id"]: to_blender(b["origin"], scale) for b in bones}
    children = {}
    for b in bones:
        children.setdefault(b["parent"], []).append(b["id"])

    tails = {}
    for b in bones:
        bid = b["id"]
        head = heads[bid]
        target = None

        kids = [k for k in children.get(bid, []) if k in heads]
        if kids:
            mean = [sum(heads[k][i] for k in kids) / len(kids) for i in range(3)]
            if _distance(mean, head) > MIN_BONE_LENGTH:
                target = mean

        if target is None:
            own = verts_by_bone.get(bid)
            if own:
                far = max(own, key=lambda p: _distance(p, head))
                if _distance(far, head) > MIN_BONE_LENGTH:
                    # Not all the way to the far corner -- a bone drawn the
                    # whole length of its armour swamps the one below it.
                    target = [head[i] + (far[i] - head[i]) * 0.7
                              for i in range(3)]

        if target is None:
            # A pivot with no geometry and a child sitting on the same point:
            # a shoulder, a hip. Point it down its own frame's -Y, which is
            # the way the limb below it hangs, and give it enough length to
            # be clickable on a machine this size.
            axis = b["axes"][1]
            down = to_blender([-axis[0], -axis[1], -axis[2]], 1.0)
            target = [head[i] + down[i] * stub for i in range(3)]

        tails[bid] = tuple(target)
    return heads, tails


def _distance(a, b):
    return sum((a[i] - b[i]) ** 2 for i in range(3)) ** 0.5


def _srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def build_blend(machine, out_path, scale):
    import bpy
    from mathutils import Vector

    # Start from nothing rather than from Blender's default cube-and-lamp.
    bpy.ops.wm.read_factory_settings(use_empty=True)

    name = machine["name"]
    bones = machine["bones"]
    by_id = {b["id"]: b for b in bones}

    verts, faces, face_bone, face_palette = build_mesh(machine, scale)

    verts_by_bone = {}
    for face, bone in zip(faces, face_bone):
        verts_by_bone.setdefault(bone, []).extend(verts[i] for i in face)
    stub = max(MIN_BONE_LENGTH * 4.0,
               machine["height"] * scale * 0.035)
    heads, tails = bone_tails(machine, bones, verts_by_bone, scale, stub)

    # --- the armature ----------------------------------------------------
    arm_data = bpy.data.armatures.new(name + " rig")
    arm_obj = bpy.data.objects.new(name + " rig", arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    edit = {}
    # Parents before children, so a bone always has something to hang off.
    order = sorted(by_id, key=lambda b: _depth(by_id, b))
    for bid in order:
        spec = by_id[bid]
        bone = arm_data.edit_bones.new(spec["name"])
        bone.head = Vector(heads[bid])
        bone.tail = Vector(tails[bid])
        if (bone.tail - bone.head).length < MIN_BONE_LENGTH:
            bone.tail = bone.head + Vector((0.0, 0.0, MIN_BONE_LENGTH * 2))
        if spec["parent"] in edit:
            bone.parent = edit[spec["parent"]]
            # Left unconnected on purpose: a hip is not at the end of the
            # root, and forcing the heads together would move the joints.
            bone.use_connect = False
        edit[bid] = bone
    bpy.ops.object.mode_set(mode="OBJECT")

    # --- the mesh --------------------------------------------------------
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()

    mesh_obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(mesh_obj)

    # One material per palette index actually used, coloured from the
    # project's own fallback table.
    palette = {p["index"]: p for p in machine["palette"]}
    slot_of = {}
    for index in sorted(set(face_palette)):
        entry = palette.get(index, {"rgb": [128, 128, 128], "glow": False})
        rgb = entry["rgb"]
        mat = bpy.data.materials.new("%s.%d" % (name.split()[0].lower(), index))
        # The palette is what a screen shows, so it is sRGB; Blender wants
        # base colour linear. Feeding one to the other unconverted is what
        # makes an exported model come out looking bleached.
        lin = [_srgb_to_linear(c / 255.0) for c in rgb]
        mat.diffuse_color = (lin[0], lin[1], lin[2], 1.0)
        # Blender 4.x hands back a material with no node tree and wants
        # use_nodes set; 5.0 always has one and deprecates the flag. Asking
        # for the tree first gets both right and warns on neither.
        if getattr(mat, "node_tree", None) is None:
            mat.use_nodes = True
        tree = getattr(mat, "node_tree", None)
        bsdf = tree.nodes.get("Principled BSDF") if tree else None
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (lin[0], lin[1],
                                                       lin[2], 1.0)
            if entry.get("glow"):
                # A vent or a sensor: it is lit, and painting it as matte
                # plastic is what makes an exported mech look like a toy.
                bsdf.inputs["Roughness"].default_value = 0.3
                for socket, value in (("Emission Color",
                                       (lin[0], lin[1], lin[2], 1.0)),
                                      ("Emission Strength", 4.0)):
                    if socket in bsdf.inputs:
                        bsdf.inputs[socket].default_value = value
            else:
                bsdf.inputs["Roughness"].default_value = 0.42
                if "Metallic" in bsdf.inputs:
                    bsdf.inputs["Metallic"].default_value = 0.35
        slot_of[index] = len(mesh.materials)
        mesh.materials.append(mat)
    for poly, index in zip(mesh.polygons, face_palette):
        poly.material_index = slot_of[index]

    # Faceted, because the machines are: smoothing plate armour reads as
    # melted rather than as machined.
    for poly in mesh.polygons:
        poly.use_smooth = False

    # --- the skin --------------------------------------------------------
    groups = {}
    for spec in bones:
        groups[spec["id"]] = mesh_obj.vertex_groups.new(name=spec["name"])
    assigned = {}
    for face, bone in zip(faces, face_bone):
        for vi in face:
            assigned.setdefault(bone, set()).add(vi)
    for bone, indices in assigned.items():
        if bone in groups:
            groups[bone].add(sorted(indices), 1.0, "REPLACE")

    mesh_obj.parent = arm_obj
    modifier = mesh_obj.modifiers.new(name="Armature", type="ARMATURE")
    modifier.object = arm_obj
    modifier.use_vertex_groups = True

    # What it is, for whoever opens the file without this script to hand.
    mesh_obj["mech_class"] = machine["class"]
    mesh_obj["mech_height_m"] = machine["height"] / machine["metre"]
    arm_obj["mech_name"] = name

    arm_obj.show_in_front = True
    arm_data.display_type = "OCTAHEDRAL"

    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(out_path))
    return mesh_obj, arm_obj, len(verts), len(faces)


def _depth(by_id, bid):
    depth = 0
    while bid in by_id and by_id[bid]["parent"] in by_id:
        bid = by_id[bid]["parent"]
        depth += 1
    return depth


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("machine", nargs="?", default="Lilia 07",
                    help="roster name, e.g. 'Lilia 07'")
    ap.add_argument("-o", "--out", default=None, help="output .blend")
    ap.add_argument("--keep", default=None,
                    help="directory to keep the built exporter and JSON in")
    args = ap.parse_args()

    machine = dump_machine(args.machine, args.keep)
    # Units per metre, so the model opens life-sized rather than in the
    # game's own 250-to-the-metre.
    scale = 1.0 / machine["metre"]
    out = args.out or (args.machine.replace(" ", "_").replace(".", "") +
                       ".blend")

    mesh_obj, arm_obj, nverts, nfaces = build_blend(machine, out, scale)
    print("%s: %d verts, %d faces, %d bones -> %s"
          % (machine["name"], nverts, nfaces, len(machine["bones"]),
             os.path.abspath(out)))
    print("  %.2f m tall" % (machine["height"] / machine["metre"]))
    # Say which format this is.  Blender refuses a file from a version
    # later than itself, and since the format follows whichever bpy is
    # installed rather than anything chosen here, writing an unopenable
    # file is silent and easy.  The docstring above has warned about it
    # from the start; a warning nobody is shown is not a warning.
    # bpy is imported inside build_blend rather than at module scope, so
    # that a bad argument fails before paying to load it.  By here it has
    # been imported and this is free.
    import bpy

    print("  Blender %d.%d format -- older Blenders cannot open this"
          % bpy.app.version[:2])


if __name__ == "__main__":
    main()
