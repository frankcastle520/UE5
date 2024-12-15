# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will find the given RenderGrid asset and render it, while logging the current rendering progress each frame.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -RenderGrid=<path_to_render_grid> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderGivenAndShowProgression.py" -RenderGrid="/Game/Foo/MyRenderGrid" -stdout


def get_given_render_grid_path() -> str:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return cmd_parameters['RenderGrid']
    except:
        unreal.log_error("Missing '-RenderGrid=/Game/Foo/MyRenderGrid' argument")
        return ""


def start_rendering_given_render_grid():
    render_grid_path = get_given_render_grid_path()
    if render_grid_path != "":
        grid = unreal.RenderGridDeveloperLibrary.get_render_grid_asset(render_grid_path)
        if grid is not None:
            grid.render()
        else:
            unreal.log_error("Invalid render grid: " + render_grid_path)


def on_tick(dt: float):
    queue = unreal.RenderGridQueue.get_currently_rendering_queue()
    if queue is not None:
        unreal.log_warning("Progression of current render grid:  " + str(queue.get_status_percentage()) + "% (" + str(queue.get_jobs_completed_count()) + "/" + str(queue.get_jobs_count()) + ")")


if __name__ == '__main__':
    unreal.register_slate_post_tick_callback(on_tick)
    start_rendering_given_render_grid()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
