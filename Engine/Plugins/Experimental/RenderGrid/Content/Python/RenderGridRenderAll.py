# Copyright Epic Games, Inc. All Rights Reserved.
import unreal


# This script will loop through all the RenderGrid assets in the project, and render each one.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderAll.py" -stdout


def start_rendering_all_render_grids():
    for grid in unreal.RenderGridDeveloperLibrary.get_all_render_grid_assets():
        grid.render()


if __name__ == '__main__':
    start_rendering_all_render_grids()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
