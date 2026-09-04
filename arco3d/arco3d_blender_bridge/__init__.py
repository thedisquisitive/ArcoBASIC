"""Arco3D Blender Bridge (RFC-0051) -- File > Import/Export > Arcology 3D (.a3d).

A classic bl_info-based add-on (installed via Edit > Preferences > Add-ons > Install..., pointing
at a zip of this whole directory, or by copying this directory into Blender's own addons folder)
rather than the newer Extensions-Platform manifest.toml format -- broader compatibility across
Blender versions per RFC-0051 Section 22's own "isolate compatibility code, don't chase every new
API" guidance, and the simplest path that still satisfies this RFC's own Definition of Done
("the add-on installs on the designated supported Blender version").
"""

import bpy
from bpy.types import Operator
from bpy_extras.io_utils import ImportHelper, ExportHelper
from bpy.props import StringProperty

from . import a3d_parser
from . import a3d_import
from . import a3d_export

bl_info = {
    "name": "Arco3D Bridge (.a3d)",
    "author": "Arcology Project",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "File > Import/Export",
    "description": "Import/export Arcology 3D (.a3d) assets -- geometry, rigs, skin weights, and poses (RFC-0050/RFC-0051)",
    "category": "Import-Export",
}


class IMPORT_OT_arco3d(Operator, ImportHelper):
    bl_idname = "import_scene.arco3d"
    bl_label = "Import Arcology 3D (.a3d)"
    bl_description = "Import an Arcology 3D asset (geometry, rig, skin weights, pose)"
    bl_options = {'REGISTER', 'UNDO'}

    filename_ext = ".a3d"
    filter_glob: StringProperty(default="*.a3d", options={'HIDDEN'})

    def execute(self, context):
        try:
            report = a3d_import.import_a3d(self.filepath, context)
        except a3d_parser.A3DError as error:
            self.report({'ERROR'}, "Arco3D import failed: %s" % error)
            return {'CANCELLED'}
        if report.warnings:
            self.report({'WARNING'}, report.summary())
        else:
            self.report({'INFO'}, report.summary())
        return {'FINISHED'}


class EXPORT_OT_arco3d(Operator, ExportHelper):
    bl_idname = "export_scene.arco3d"
    bl_label = "Export Arcology 3D (.a3d)"
    bl_description = "Export the selected mesh objects to an Arcology 3D asset (portable subset -- see RFC-0051 Section 20)"
    bl_options = {'REGISTER'}

    filename_ext = ".a3d"
    filter_glob: StringProperty(default="*.a3d", options={'HIDDEN'})

    def execute(self, context):
        objects = context.selected_objects if context.selected_objects else context.scene.objects
        report = a3d_export.ExportReport()
        try:
            a3d_export.export_a3d(self.filepath, objects, context.scene.name or "ExportedAsset", report)
        except OSError as error:
            self.report({'ERROR'}, "Arco3D export failed: %s" % error)
            return {'CANCELLED'}
        if report.warnings:
            self.report({'WARNING'}, report.summary())
        else:
            self.report({'INFO'}, report.summary())
        return {'FINISHED'}


def _menu_import(self, context):
    self.layout.operator(IMPORT_OT_arco3d.bl_idname, text="Arcology 3D (.a3d)")


def _menu_export(self, context):
    self.layout.operator(EXPORT_OT_arco3d.bl_idname, text="Arcology 3D (.a3d)")


_classes = (IMPORT_OT_arco3d, EXPORT_OT_arco3d)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_import.append(_menu_import)
    bpy.types.TOPBAR_MT_file_export.append(_menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(_menu_import)
    bpy.types.TOPBAR_MT_file_export.remove(_menu_export)
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
