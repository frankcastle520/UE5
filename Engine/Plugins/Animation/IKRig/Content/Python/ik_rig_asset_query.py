import unreal

def get_fbik_solver_index(controller):

    num_solvers = controller.get_num_solvers()

    if num_solvers <= 0:
        print("no solvers")
        return None

    for index in range(num_solvers):
        solver_type = controller.get_solver_at_index(index)
        if type(solver_type) == unreal.IKRigFBIKSolver and controller.get_solver_enabled(index):
            return index

    return None

def get_fbik_solver_goals(controller, fbik_index):
    fbik_goals = []
    all_goals = controller.get_all_goals()
    for goal in all_goals:
        if controller.is_goal_connected_to_solver(goal.goal_name, fbik_index):
            fbik_goals.append(goal)
    return fbik_goals

def get_fbik_goal_settings(controller, goals, fbik_index):
    fbik_goal_settings = []
    for goal in goals:
        fbik_goal_settings.append(controller.get_goal_settings_for_solver(goal.goal_name, fbik_index))
    return fbik_goal_settings

def get_bone_hierarchy(controller):
    skm = controller.get_skeletal_mesh()
    ref_pose = skm.skeleton.get_reference_pose()

    bone_names = ref_pose.get_bone_names()

    return bone_names

def get_fbik_bone_settings(controller, fbik_index):
    bones = get_bone_hierarchy(controller)
    bone_settings = []
    for bone in bones:
        bone_setting = controller.get_bone_settings(bone, fbik_index)
        if bone_setting:
            bone_settings.append(bone_setting)
    return bone_settings

def get_excluded_bones(controller):
    bones = get_bone_hierarchy(controller)
    excluded_bones = []
    for bone in bones:
        if controller.get_bone_excluded(bone):
            excluded_bones.append(bone)
    return excluded_bones

def check_skeletal_mesh_compatibility(ik_rig, skeletal_mesh):
    ikr_controller = unreal.IKRigController.get_controller(ik_rig)
    return ikr_controller.is_skeletal_mesh_compatible(skeletal_mesh)

def run_asset_query(ik_rig):

    ikr_controller = unreal.IKRigController.get_controller(ik_rig)

    fbik_index = get_fbik_solver_index(ikr_controller)

    if fbik_index is None:
        return

    fbik_solver = ikr_controller.get_solver_at_index(fbik_index)
    goals = get_fbik_solver_goals(ikr_controller, fbik_index)
    goal_settings = get_fbik_goal_settings(ikr_controller, goals, fbik_index)
    bone_settings = get_fbik_bone_settings(ikr_controller, fbik_index)
    excluded_bones = get_excluded_bones(ikr_controller)

    return_value = {"Solver": fbik_solver, 
                    "Goals": goals,
                    "Goal Settings": goal_settings,
                    "Bone Settings": bone_settings,
                    "Excluded Bones": excluded_bones}

    return return_value