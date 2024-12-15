# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will loop through all the RenderGrid assets in the project, and log the data of each one.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridLogAll.py" -stdout


def log_all_render_grids_manually():
    for grid in unreal.RenderGridDeveloperLibrary.get_all_render_grid_assets():
        for job in grid.get_render_grid_jobs():
            unreal.log_warning("Job \"" + job.get_job_id() + "\"")
            values = job.get_remote_control_values()
            for fieldId in values.keys():
                unreal.log_warning(" > [" + fieldId.to_string() + "] " + job.get_remote_control_label_from_field_id(fieldId) + " = " + values.get(fieldId))


def log_all_render_grids_builtin():
    for grid in unreal.RenderGridDeveloperLibrary.get_all_render_grid_assets():
        unreal.log_warning(grid.to_debug_string())


if __name__ == '__main__':
    log_all_render_grids_builtin()
