# Copyright Epic Games, Inc. All Rights Reserved.
import unreal
import json


# This script will find the given RenderGrid asset, set the given remote control properties in it temporarily, and render it.
#
# REQUIREMENTS:
#    Requires the "Python Editor Script Plugin" to be enabled in your project.
#
# USAGE:
#   Use the following command line argument to launch this:
#     <path_to_unreal_engine>/UnrealEditor-Cmd.exe <path_to_uproject> -ExecutePythonScript=<path_to_this_script> -RenderGrid=<path_to_render_grid> -RemoteControlProperties=<remote_control_property_json_data> -stdout
#   Example:
#     C:/UE5/Engine/Binaries/Win64/UnrealEditor-Cmd.exe "C:/MyProject/MyProject.uproject" -ExecutePythonScript="C:/UE5/Engine/Plugins/Experimental/RenderGrid/Content/Python/RenderGridRenderGivenAndOverrideValues.py" -RenderGrid="/Game/Foo/MyRenderGrid" -RemoteControlProperties="{'Label Of Int Property':10, 'Label Of Color Property':{'R':1, 'G':0, 'B':1, 'A':1}}" -stdout


def get_given_render_grid_path() -> str:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        return cmd_parameters['RenderGrid']
    except:
        unreal.log_error("Missing '-RenderGrid=/Game/Foo/MyRenderGrid' argument")
        return ""


def get_given_remote_control_properties() -> dict[str, str]:
    (cmd_tokens, cmd_switches, cmd_parameters) = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    try:
        json_string = cmd_parameters['RemoteControlProperties']
    except:
        unreal.log_error("Missing '-RemoteControlProperties=\"{}\"' argument")
        return {}
    try:
        json_string = json_string.replace("'", '"').replace('\\"', "'").replace("\\'", '\\"')
        unreal.log_warning("Parsing JSON: " + json_string)
        json_object = json.loads(json_string)
        result = {}
        for key in json_object.keys():
            result[key] = json.dumps(json_object[key])
        return result
    except BaseException as e:
        unreal.log_error("-RemoteControlProperties argument contained invalid data: " + str(e))
        return {}


def start_rendering_given_render_grid():
    render_grid_path = get_given_render_grid_path()
    if render_grid_path != "":
        grid = unreal.RenderGridDeveloperLibrary.get_render_grid_asset(render_grid_path)
        if grid is not None:
            rc_props = get_given_remote_control_properties()
            for job in grid.get_enabled_render_grid_jobs():
                for key in rc_props.keys():
                    field_id = job.get_remote_control_field_id_from_label(key)
                    if field_id is not None:
                        job.set_remote_control_value(field_id, rc_props[key])
                    else:
                        unreal.log_error("Could not find RemoteControl property with label: \"" + key + "\"")
            grid.render()
        else:
            unreal.log_error("Invalid render grid: " + render_grid_path)


if __name__ == '__main__':
    start_rendering_given_render_grid()
    unreal.RenderGridQueue.close_editor_on_completion()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
