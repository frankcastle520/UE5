# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will loop through all the RenderGrid assets in the project, and render a single frame of each one.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -Frame=<frame_number_to_render> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderAllSingleFrame.py" -Frame="0" -stdout


def get_given_frame() -> int:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return int(cmd_parameters['Frame'])
    except:
        unreal.log_error("Missing '-Frame=\"0\"' argument")
        return 0


def start_rendering_all_render_grids():
    frame = get_given_frame()
    for grid in unreal.RenderGridDeveloperLibrary.get_all_render_grid_assets():
        grid.render_single_frame(frame)


if __name__ == '__main__':
    start_rendering_all_render_grids()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
