from ModuleConverterInterface import ModuleConverterInterface
import unreal as ue
from unreal import FXConverterUtilitiesLibrary as ueFxUtils
import CascadeToNiagaraHelperMethods as c2nUtils
import Paths


class CascadeMeshRotationConverter(ModuleConverterInterface):

    @classmethod
    def get_input_cascade_module(cls):
        return ue.ParticleModuleMeshRotation

    @classmethod
    def convert(cls, args):
        cascade_module = args.get_cascade_module()
        emitter = args.get_niagara_emitter_context()

        # get all properties from the cascade mesh rotation module
        # noinspection PyTypeChecker
        (rotation,
         b_inherit_parent_rotation
         ) = ueFxUtils.get_particle_module_mesh_rotation_props(cascade_module)

        # make an input for the cascade rotation and inherit parent rotation
        rotation_input = c2nUtils.create_script_input_for_distribution(rotation)
        parent_rotation_input = ueFxUtils.create_script_input_bool(b_inherit_parent_rotation)
        
        script_asset = ueFxUtils.create_asset_data(Paths.script_init_mesh_orientation)
        script_args = ue.CreateScriptContextArgs(script_asset)
        init_rotation_script = emitter.find_or_add_module_script(
            "Initialize Mesh Orientation", 
            script_args,
            ue.ScriptExecutionCategory.PARTICLE_SPAWN)
        
        init_rotation_script.set_parameter("Rotation", rotation_input)
        init_rotation_script.set_parameter("Inherit Parent Rotation", parent_rotation_input)