# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will find the given RenderGrid asset and render a single frame of it.
# The frame is given percentage-wise, a value between 0.0 and 1.0, which will determine the actual frame number that will be rendered. A value of 0.0 is the first frame, 1.0 is the last frame, 0.5 is the frame in the middle, etc.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -RenderGrid=<path_to_render_grid> -FramePosition=<value_between_0_and_1_for_which_frame_to_render_percentage_wise> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderGivenSingleFramePosition.py" -RenderGrid="/Game/Foo/MyRenderGrid" -FramePosition="0.0" -stdout


def get_given_render_grid_path() -> str:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return cmd_parameters['RenderGrid']
    except:
        unreal.log_error("Missing '-RenderGrid=/Game/Foo/MyRenderGrid' argument")
        return ""


def get_given_frame_position() -> float:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return min(1.0, max(0.0, float(cmd_parameters['FramePosition'])))
    except:
        unreal.log_error("Missing '-FramePosition=\"0.0\"' argument")
        return 0.0


def start_rendering_given_render_grid():
    render_grid_path = get_given_render_grid_path()
    frame_position = get_given_frame_position()
    if render_grid_path != "":
        grid = unreal.RenderGridDeveloperLibrary.get_render_grid_asset(render_grid_path)
        if grid is not None:
            grid.render_single_frame_position(frame_position)
        else:
            unreal.log_error("Invalid render grid: " + render_grid_path)


if __name__ == '__main__':
    start_rendering_given_render_grid()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
