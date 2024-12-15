"""
This script will launch on Editor startup if Control Rig is enabled. The executions below are functional
examples on how users can use Python to extend the Control Rig Editor.
"""

import RigHierarchy.add_controls_for_selected
import RigHierarchy.add_null_above_selected
import RigHierarchy.align_items
import RigHierarchy.rename_items
import RigHierarchy.set_bone_reference_pose

RigHierarchy.add_controls_for_selected.run()
RigHierarchy.add_null_above_selected.run()
RigHierarchy.align_items.run()
RigHierarchy.rename_items.run()
RigHierarchy.set_bone_reference_pose.run()

if hasattr(unreal, 'RigVMUserWorkflowProvider'):
    import ControlRigWorkflows.workflow_aim_bone
    import ControlRigWorkflows.workflow_two_bone_ik
    import ControlRigWorkflows.workflow_add_items_to_array
    import ControlRigWorkflows.workflow_parent_constraint
    import ControlRigWorkflows.workflow_set_control_shape
    import ControlRigWorkflows.workflow_reverse_array
    import ControlRigWorkflows.workflow_deformation_rig_preset
    import ControlRigWorkflows.workflow_fbik_import_ik_rig
    ControlRigWorkflows.workflow_aim_bone.provider.register()
    ControlRigWorkflows.workflow_two_bone_ik.provider.register()
    ControlRigWorkflows.workflow_add_items_to_array.provider.register()
    ControlRigWorkflows.workflow_parent_constraint.provider.register()
    ControlRigWorkflows.workflow_set_control_shape.provider.register()
    ControlRigWorkflows.workflow_reverse_array.provider.register()
    ControlRigWorkflows.workflow_deformation_rig_preset.provider.register()
    ControlRigWorkflows.workflow_fbik_import_ik_rig.provider.register()