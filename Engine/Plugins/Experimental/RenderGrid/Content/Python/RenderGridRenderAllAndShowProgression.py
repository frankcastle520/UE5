# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will loop through all the RenderGrid assets in the project, and render each one, while logging the current rendering progress each frame.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderAllAndShowProgression.py" -stdout


def start_rendering_all_render_grids():
    for grid in unreal.RenderGridDeveloperLibrary.get_all_render_grid_assets():
        grid.render()


def on_tick(dt: float):
    queue = unreal.RenderGridQueue.get_currently_rendering_queue()
    if queue is not None:
        remaining_count_after_current = unreal.RenderGridQueue.get_remaining_rendering_queues_count() - 1
        unreal.log_warning("Progression of current render grid (" + str(remaining_count_after_current) + " grids in queue):  " + str(queue.get_status_percentage()) + "% (" + str(queue.get_jobs_completed_count()) + "/" + str(queue.get_jobs_count()) + ")")


if __name__ == '__main__':
    unreal.register_slate_post_tick_callback(on_tick)
    start_rendering_all_render_grids()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
