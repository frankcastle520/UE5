'''
	Implement a workflow for a node in Python

    The action will set up the Full Body IK node by importing an IK Rig asset
'''

import unreal
import ik_rig_asset_query

@unreal.uclass()
class import_ik_rig_options(unreal.Object):
    '''Custom class for IK Rig import settings'''
    ik_rig = unreal.uproperty(unreal.IKRigDefinition, meta={"DisplayName": "IK Rig", "DisplayPriorty": "1"})
    import_bone_settings = unreal.uproperty(bool, meta={"DisplayName": "Import Bone Settings", "DisplayPriorty": "2"})
    import_excluded_bones = unreal.uproperty(bool, meta={"DisplayName": "Import Excluded Bones", "DisplayPriorty": "3"})
    import_effectors = unreal.uproperty(bool, meta={"DisplayName": "Import Effectors", "DisplayPriorty": "4"})
    create_transform_nodes_for_effectors = unreal.uproperty(bool, meta={"DisplayName": "Create Transform Nodes for Effectors", "DisplayPriorty": "5"})

    def _post_init(self):
        '''Set default class values after initialization'''
        self.import_excluded_bones = True
        self.import_bone_settings = True
        self.import_effectors = True
        self.create_transform_nodes_for_effectors = True
        self.ik_rig = None

class provider:

    provider_handles = {}
    user_data = import_ik_rig_options()

    def __call__(self, in_node):
        # create a new workflow
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.NODE_CONTEXT
        workflow.title = 'Configure from IK Rig Asset'
        workflow.tooltip = 'Configure the FBIK node from an IK Rig asset'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()

        # return a list of workflows for this provider
        return [workflow]

    def perform_user_workflow(self, in_options, in_controller):

        node = in_options.subject
        blueprint = node.get_typed_outer(unreal.ControlRigBlueprint)
        if blueprint:
            hierarchy = blueprint.hierarchy
        else:
            hierarchy = in_options.hierarchy
        
        hierarchy_controller = hierarchy.get_controller()

        object_details_view_options = unreal.EditorDialogLibraryObjectDetailsViewOptions()
        object_details_view_options.show_object_name = False
        object_details_view_options.allow_search = False
        confirmed = unreal.EditorDialog.show_object_details_view(
            "Import IK Rig Options",
            self.user_data,
            object_details_view_options)

        if confirmed:

            # Check that the IK Rig is compatible with the preview SKM in the CR
            cr_mesh = blueprint.get_preview_mesh()
            ikr_controller = unreal.IKRigController.get_controller(self.user_data.ik_rig)
            if not ikr_controller.is_skeletal_mesh_compatible(cr_mesh):
                unreal.EditorDialog.show_message("IK Rig Import Error", "IK Rig was not imported due to an incompatible Skeletal Mesh", unreal.AppMsgType.OK, unreal.AppReturnType.OK, unreal.AppMsgCategory.ERROR)
                return False
            
            try:
                ikr_data = ik_rig_asset_query.run_asset_query(self.user_data.ik_rig)
                solver = ikr_data["Solver"]
                goals = ikr_data["Goals"]
                goal_settings = ikr_data["Goal Settings"]
                bone_settings = ikr_data["Bone Settings"]
                excluded_bones = ikr_data["Excluded Bones"]
            except:
                unreal.EditorDialog.show_message("IK Rig Import Error", "IK Rig does not have sufficient data to create FBIK", unreal.AppMsgType.OK, unreal.AppReturnType.OK, unreal.AppMsgCategory.ERROR)
                return False

            # create a new node to base our settings on
            new_defaults = unreal.RigUnit_PBIK()

            # init the settings based on the current defaults on the node
            # this makes sure we keep settings that we don't want to change
            new_defaults.import_text(node.get_struct_default_value())

            # import bone settings
            if self.user_data.import_bone_settings:
                node_bone_settings = []
                for bone_setting in bone_settings:
                    node_bone_setting = unreal.PBIKBoneSetting()
                    node_bone_setting.set_editor_property("bone", bone_setting.bone)
                    node_bone_setting.set_editor_property("use_preferred_angles", bone_setting.use_preferred_angles)
                    node_bone_setting.set_editor_property("max_x", bone_setting.max_x)
                    node_bone_setting.set_editor_property("max_y", bone_setting.max_y)
                    node_bone_setting.set_editor_property("max_z", bone_setting.max_z)
                    node_bone_setting.set_editor_property("min_x", bone_setting.min_x)
                    node_bone_setting.set_editor_property("min_y", bone_setting.min_y)
                    node_bone_setting.set_editor_property("min_z", bone_setting.min_z)
                    node_bone_setting.set_editor_property("position_stiffness", bone_setting.position_stiffness)
                    node_bone_setting.set_editor_property("rotation_stiffness", bone_setting.rotation_stiffness)
                    node_bone_setting.set_editor_property("preferred_angles", bone_setting.preferred_angles)
                    node_bone_setting.set_editor_property("x", bone_setting.x)
                    node_bone_setting.set_editor_property("y", bone_setting.y)
                    node_bone_setting.set_editor_property("z", bone_setting.z)
                    node_bone_settings.append(node_bone_setting)
                new_defaults.bone_settings = node_bone_settings
            
            # import excluded bones
            if self.user_data.import_excluded_bones:
                new_defaults.excluded_bones = excluded_bones

            # set the defaults on the new node based on our local unit struct
            if not in_controller.set_unit_node_defaults(node, new_defaults.export_text()):
                return False

            # import effector settings
            if self.user_data.import_effectors:
                
                # change control shapes to yellow squares
                default_setting = unreal.RigControlSettings()
                default_setting.animation_type = unreal.RigControlAnimationType.ANIMATION_CONTROL
                default_setting.control_type = unreal.RigControlType.EULER_TRANSFORM
                default_setting.shape_color = unreal.LinearColor(r = 1.0, g = 1.0, b = 0.0, a = 1.0)
                default_setting.shape_name = "Box_Thick"
                default_setting.is_transient_control = False
                default_value = hierarchy.make_control_value_from_euler_transform(unreal.EulerTransform(scale=[1, 1, 1]))

                effectors_pin = node.find_pin('Effectors')
                node_index_multipler = 200

                for goal in goals:

                    # Get bone
                    bone_name = goal.bone_name
                    bone_key = unreal.RigElementKey(name = bone_name, type = unreal.RigElementType.BONE)

                    # Confirm that it exists in the hierarchy
                    if not hierarchy.contains(bone_key):
                            continue
                    
                    # Create effector pin on FBIK node
                    goal_index = goals.index(goal)
                    goal_setting = goal_settings[goal_index]
                    in_controller.add_array_pin(effectors_pin.get_pin_path(use_node_path = True))
                    effectors_sub_pins = effectors_pin.get_sub_pins()
                    sub_pin = effectors_sub_pins[goal_index]
                    effector_pin_path = sub_pin.get_pin_path()

                    # Set all settings for FBIK node
                    in_controller.set_pin_default_value("{0}.Bone".format(effector_pin_path), str(goal.bone_name))
                    in_controller.set_pin_default_value("{0}.PositionAlpha".format(effector_pin_path), str(goal.position_alpha))
                    in_controller.set_pin_default_value("{0}.RotationAlpha".format(effector_pin_path), str(goal.rotation_alpha))
                    in_controller.set_pin_default_value("{0}.StrengthAlpha".format(effector_pin_path), str(goal_setting.strength_alpha))
                    in_controller.set_pin_default_value("{0}.PullChainAlpha".format(effector_pin_path), str(goal_setting.pull_chain_alpha))
                    in_controller.set_pin_default_value("{0}.PinRotation".format(effector_pin_path), str(goal_setting.pin_rotation))

                    if self.user_data.create_transform_nodes_for_effectors:

                        # get goal name
                        ctrl_name = goal.goal_name
                        
                        # create controls with goal names where the bones are
                        effector_ctrl_key = hierarchy_controller.add_control(ctrl_name, '',
                                                                            default_setting, default_value)
                        transform = hierarchy.get_global_transform(bone_key, initial = True)
                        hierarchy.set_control_offset_transform(effector_ctrl_key, transform, initial = True)

                        trans_node_position = node.get_position() + unreal.Vector2D(-340, (node_index_multipler*(goal_index+1)))

                        get_transform_defaults = unreal.RigUnit_GetTransform()
                        get_transform_defaults.space = unreal.RigVMTransformSpace.GLOBAL_SPACE
                        get_transform_defaults.initial = False
                        
                        get_transform_defaults.item = effector_ctrl_key

                        # add the node
                        get_transform_node = in_controller.add_unit_node_with_defaults(get_transform_defaults.static_struct(), get_transform_defaults.export_text(), 'Execute', trans_node_position)

                        if get_transform_node == None:
                            continue

                        transform_pin_path = get_transform_node.find_pin('Transform').get_pin_path()

                        # link the get transform to the effector on the two bone ik node
                        if not in_controller.add_link(transform_pin_path, "{0}.Transform".format(effector_pin_path)):
                            continue
            
            # Import root bone & fbik settings
            # Cannot use above import on FBIK solver settings or root bone
            in_controller.set_pin_default_value(node.find_pin('Root').get_pin_path(), str(solver.root_bone))
            in_controller.set_pin_default_value(node.find_pin('Settings.bAllowStretch').get_pin_path(), str(solver.allow_stretch))
            in_controller.set_pin_default_value(node.find_pin('Settings.GlobalPullChainAlpha').get_pin_path(), str(solver.pull_chain_alpha))
            in_controller.set_pin_default_value(node.find_pin('Settings.Iterations').get_pin_path(), str(solver.iterations))
            in_controller.set_pin_default_value(node.find_pin('Settings.MassMultiplier').get_pin_path(), str(solver.mass_multiplier))
            in_controller.set_pin_default_value(node.find_pin('Settings.MaxAngle').get_pin_path(), str(solver.max_angle))
            in_controller.set_pin_default_value(node.find_pin('Settings.OverRelaxation').get_pin_path(), str(solver.over_relaxation))
            in_controller.set_pin_default_value(node.find_pin('Settings.RootBehavior').get_pin_path(), str(solver.root_behavior.get_display_name()))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.RotationAlpha').get_pin_path(), str(solver.pre_pull_root_settings.rotation_alpha))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.RotationAlphaX').get_pin_path(), str(solver.pre_pull_root_settings.rotation_alpha_x))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.RotationAlphaY').get_pin_path(), str(solver.pre_pull_root_settings.rotation_alpha_y))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.RotationAlphaZ').get_pin_path(), str(solver.pre_pull_root_settings.rotation_alpha_z))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.PositionAlpha').get_pin_path(), str(solver.pre_pull_root_settings.position_alpha))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.PositionAlphaX').get_pin_path(), str(solver.pre_pull_root_settings.position_alpha_x))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.PositionAlphaY').get_pin_path(), str(solver.pre_pull_root_settings.position_alpha_y))
            in_controller.set_pin_default_value(node.find_pin('Settings.PrePullRootSettings.PositionAlphaZ').get_pin_path(), str(solver.pre_pull_root_settings.position_alpha_z))     
            
            return True

        return False

    @classmethod
    def register(cls):
        # retrieve the node we want to add a workflow to
        unit_struct = unreal.RigUnit_PBIK.static_struct()

        # create an empty callback and bind an instance of this class to it
        provider_callback = unreal.RigVMUserWorkflowProvider()
        provider_callback.bind_callable(cls())

        # remember the registered provider handle so we can unregister later
        handle = unreal.RigVMUserWorkflowRegistry.get().register_provider(unit_struct, provider_callback)

        # we also store the callback on the class 
        # so that it doesn't get garbage collected
        cls.provider_handles[handle] = provider_callback

    @classmethod
    def unregister(cls):
        for (handle, provider) in cls.provider_handles:
            unreal.RigVMUserWorkflowRegistry.get().unregister_provider(handle)
        cls.provider_handles = {}
