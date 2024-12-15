# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will find the given RenderGrid asset and render a single frame of it.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -RenderGrid=<path_to_render_grid> -Frame=<frame_number_to_render> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderGivenSingleFrame.py" -RenderGrid="/Game/Foo/MyRenderGrid" -Frame="0" -stdout


def get_given_render_grid_path() -> str:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return cmd_parameters['RenderGrid']
    except:
        unreal.log_error("Missing '-RenderGrid=/Game/Foo/MyRenderGrid' argument")
        return ""


def get_given_frame() -> int:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return int(cmd_parameters['Frame'])
    except:
        unreal.log_error("Missing '-Frame=\"0\"' argument")
        return 0


def start_rendering_given_render_grid():
    render_grid_path = get_given_render_grid_path()
    frame = get_given_frame()
    if render_grid_path != "":
        grid = unreal.RenderGridDeveloperLibrary.get_render_grid_asset(render_grid_path)
        if grid is not None:
            grid.render_single_frame(frame)
        else:
            unreal.log_error("Invalid render grid: " + render_grid_path)


if __name__ == '__main__':
    start_rendering_given_render_grid()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
