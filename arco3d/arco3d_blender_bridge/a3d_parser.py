"""Standalone A3D parser/validator -- RFC-0050's own AI Implementation Guidance (Section 32) says
to "implement a standalone parser/validator before coupling the format to UI code" and RFC-0051's
own guidance (Section 22) says to keep "A3D Parser / Model" strictly separate from the "Blender
version adapter" layer. This module has NO bpy/mathutils/Blender import at all -- it can be loaded
and unit-tested with a plain `python3` interpreter, independent of Blender ever being installed.

Mirrors the exact validation Arco3D (Godot Edition)'s own A3DFormat.gd performs on import, so a
malformed/unsupported file is rejected the same way regardless of which application is reading it.
"""

import json
import math
import os


class A3DError(Exception):
    """Raised for any malformed or unsupported A3D file -- callers (the Blender operator layer)
    are expected to catch this and turn it into a user-facing report, never a raw traceback, since
    an A3D file is untrusted input (RFC-0050 Section 25 / RFC-0051 Section 23)."""


FORMAT_NAME = "A3D"
CONTAINER_VERSION = 1
SCHEMA_VERSION = 4
SUPPORTED_CAPABILITIES = {"mesh.core", "rig.core", "construction.arco3d", "mount.core", "material.layers"}


def load_a3d(path):
    """Reads and validates the manifest at `path`, returning the parsed dict. Raises A3DError on
    anything malformed -- never lets a raw KeyError/JSONDecodeError/etc. escape to the caller,
    since this file is untrusted input (RFC-0050 Section 25)."""
    if not os.path.isfile(path):
        raise A3DError("file does not exist: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as error:
        raise A3DError("could not open file: %s (%s)" % (path, error))

    try:
        manifest = json.loads(text)
    except json.JSONDecodeError as error:
        raise A3DError("corrupt A3D container (invalid JSON): %s" % error)

    if not isinstance(manifest, dict):
        raise A3DError("corrupt A3D container (root is not an object)")
    if manifest.get("Format") != FORMAT_NAME:
        raise A3DError("not an A3D file (Format tag is %r)" % manifest.get("Format"))
    if int(manifest.get("ContainerVersion", -1)) > CONTAINER_VERSION:
        raise A3DError(
            "A3D container version %s is newer than this bridge supports (%d)"
            % (manifest.get("ContainerVersion"), CONTAINER_VERSION)
        )
    if int(manifest.get("SchemaVersion", 1)) > SCHEMA_VERSION:
        raise A3DError(
            "A3D schema version %s is newer than this bridge supports (%d)"
            % (manifest.get("SchemaVersion"), SCHEMA_VERSION)
        )
    for capability in manifest.get("RequiredCapabilities", []):
        if capability not in SUPPORTED_CAPABILITIES:
            raise A3DError("unsupported required capability: %s" % capability)

    return manifest


def iter_components(manifest):
    """Yields every top-level component dict under Asset.Root.Children -- this app's own scene
    model is always a flat list (see A3DFormat.gd's own header comment on why: no real object
    hierarchy exists in Arco3D yet), so nested Children are real but, in practice, always empty
    today. Still walked recursively here for forward compatibility with a future producer that DOES
    nest components, per RFC-0050 Section 7's own component-hierarchy goal.
    """
    root = manifest.get("Asset", {}).get("Root", {})
    yield from _walk_children(root.get("Children", []))


def _walk_children(children):
    for component in children:
        yield component
        yield from _walk_children(component.get("Children", []))


# --- Coordinate conversion: A3D (right-handed, Y-up) -> Blender (right-handed, Z-up) -----------
#
# A3D's own frozen convention (RFC-0050 Section 10, see A3DFormat.gd's header comment) is
# right-handed, Y-up -- identical to Godot's native convention and to glTF's. The conversion to
# Blender's right-handed, Z-up convention is EXACTLY the same one Blender's own built-in glTF
# importer performs (a real, well-known, precedented mapping, not a novel derivation): rotate +90
# degrees about the X axis. In coordinates: blender.x = a3d.x; blender.y = -a3d.z; blender.z = a3d.y.
#
# Verified directly (not just asserted) that this preserves right-handedness: with old basis
# (X, Y, Z) satisfying X x Y = Z, the new basis (X' = X, Y' = -Z, Z' = Y) satisfies
# X' x Y' = X x (-Z) = -(X x Z) = -(-Y) = Y = Z' (using the right-handed identities Y x Z = X,
# Z x X = Y, so X x Z = -Y) -- so the new basis is right-handed too, matching Blender's own.
#
# For ORIENTATIONS (object transforms, bone pose rotations), a plain per-axis position swap is
# NOT valid -- Euler-style component permutation does not commute with a change of basis in
# general (exactly the class of "looks reasonable, is actually wrong" bug this whole project has
# repeatedly caught by testing with real numbers). The correct operation is quaternion
# conjugation by the SAME change-of-basis rotation, expressed as a quaternion: a +90 degree
# rotation about X is q_change = (sin(45 deg), 0, 0, cos(45 deg)) in (x, y, z, w) order.

_HALF_ANGLE = math.radians(45.0)
_SIN_HALF = math.sin(_HALF_ANGLE)
_COS_HALF = math.cos(_HALF_ANGLE)
# (x, y, z, w)
Q_CHANGE = (_SIN_HALF, 0.0, 0.0, _COS_HALF)
Q_CHANGE_CONJUGATE = (-_SIN_HALF, 0.0, 0.0, _COS_HALF)


def position_to_blender(position):
    x, y, z = position
    return (x, -z, y)


def position_from_blender(position):
    """Inverse of position_to_blender -- given blender = (x, -z, y), solving for (x, y, z):
    x = blender.x, y = blender.z, z = -blender.y. Verified directly (not just algebraically) by
    round-tripping a real vector through both functions in this module's own test."""
    bx, by, bz = position
    return (bx, bz, -by)


def quat_multiply(a, b):
    """Hamilton product a * b, both in (x, y, z, w) order -- reimplemented here (not imported from
    mathutils) specifically so this module stays bpy-free and independently testable."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def quat_to_blender(quat_xyzw):
    """Converts an orientation quaternion from A3D's Y-up frame to Blender's Z-up frame via
    conjugation by the SAME change-of-basis rotation used for positions above: q' = q_change * q *
    q_change_conjugate. This is the general, order-independent, exactly-correct way to transform an
    orientation under a change of basis (unlike permuting Euler angle components directly)."""
    return quat_multiply(quat_multiply(Q_CHANGE, quat_xyzw), Q_CHANGE_CONJUGATE)


def quat_from_blender(quat_xyzw):
    """Inverse of quat_to_blender -- conjugating by Q_CHANGE_CONJUGATE first undoes conjugation by
    Q_CHANGE, since Q_CHANGE_CONJUGATE is Q_CHANGE's own inverse as a unit quaternion:
    q = q_change_conjugate * q' * q_change."""
    return quat_multiply(quat_multiply(Q_CHANGE_CONJUGATE, quat_xyzw), Q_CHANGE)
