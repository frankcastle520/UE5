'''
    This is an example of how to extend the Right Click menu in Rig Hierarchy window in Control Rig.

    This action, Set Bone Reference Pose, is accessible by selecting any number of bones, 
    use the right mouse click menu of the Rig Hierarchy -> Set Bone Reference Pose 

    In this example, we are adding the ability to editing the Skeletal Mesh. This script will update
    the reference Pose of selected bones to match the current transform.  
        
'''

import unreal

# Each menu entry needs a custom menu owner
menu_owner = "ControlRigEditorExtension_RigHierarchy_SetBoneReferencePose"
tool_menus = unreal.ToolMenus.get()


def setup_menus():
    '''Creates the menu entry and adds to Right Click Context Menu on Rig Hierarchy'''
    if hasattr(unreal, 'ControlRigBlueprintLibrary'):
        unreal.ControlRigBlueprintLibrary.setup_all_editor_menus()
    
    menu = tool_menus.extend_menu("ControlRigEditor.RigHierarchy.ContextMenu")
    # Create a Python section within the menu
    entry = set_bone_reference_pose()
    entry.init_entry(menu_owner,
                     "ControlRigEditor.RigHierarchy.ContextMenu",
                     "Transforms", 
                     "SetBoneReferencePose", 
                     "Set Bone Reference Pose")
    menu.add_menu_entry_object(entry)


@unreal.uclass()
class set_bone_reference_pose(unreal.ToolMenuEntryScript):
    '''
    Custom Tool Menu Entry for RigControlElement creation

    Each override function will have a required context arg which will be used to find the
    specific ControlRigContextMenuContext, which will give the proper Control Rig in context.   
    '''

    @unreal.ufunction(override=True)
    def get_label(self, context):
        '''
        Override function for label of menu entry item
        '''
        control_rig_context = context.find_by_class(
            unreal.ControlRigContextMenuContext)
        return "Set Bone Reference Pose"

    @unreal.ufunction(override=True)
    def get_tool_tip(self, context):
        '''Override function for tool tip hover text'''
        return "Update the reference Pose for selected bones using the current transform.\nPython Script found in: Engine/Plugins/Animation/ControlRig/Content/Python/RigHierarchy"

    @unreal.ufunction(override=True)
    def can_execute(self, context):
        '''Override function for if the menu entry can be executed'''
        control_rig_context = context.find_by_class(
            unreal.ControlRigContextMenuContext)
        if control_rig_context:
            control_rig_bp = control_rig_context.get_control_rig_blueprint()
            if control_rig_bp:
                selected_keys = control_rig_bp.hierarchy.get_selected_keys()

                if len(selected_keys) == 0:
                    return False

                for key in selected_keys:
                    if key.type != unreal.RigElementType.BONE:
                        return False

                return True
        return False

    @unreal.ufunction(override=True)
    def execute(self, context):
        '''Override function for the menu entry execution'''

        if not unreal.EditorDialog.show_suppressable_warning_dialog("Set Bone Reference Pose",
        "You're about to edit the Preview Skeletal Mesh. \nAre you sure you want to continue?", "setbonereftransformsettings"):
            return

        control_rig_context = context.find_by_class(unreal.ControlRigContextMenuContext)
        if not control_rig_context:
            return

        control_rig_bp = control_rig_context.get_control_rig_blueprint()
        if not control_rig_bp:
            return

        hierarchy = control_rig_bp.hierarchy
        hierarchy_controller = control_rig_bp.get_hierarchy_controller()
        selected_keys = hierarchy.get_selected_keys()
        selected_keys = hierarchy.sort_keys(selected_keys)  # Sorts by index

        preview_mesh = control_rig_bp.get_preview_mesh()
        skeleton_modifier = unreal.SkeletonModifier()
        skeleton_modifier.init(preview_mesh)

        print("Editing: ", preview_mesh)
        for key in selected_keys:
            transform = hierarchy.get_local_transform(key, initial = False)
            print("Set new reference Pose", key.name, transform)
            skeleton_modifier.set_bone_transform(key.name, transform, False)

        skeleton_modifier.commit_skeleton_to_skeletal_mesh()

def run():
    """
    Executes the creation of the menu entry
    Allow iterating on this menu python file without restarting editor
    """
    tool_menus.unregister_owner_by_name(menu_owner)
    setup_menus()
    tool_menus.refresh_all_widgets()
