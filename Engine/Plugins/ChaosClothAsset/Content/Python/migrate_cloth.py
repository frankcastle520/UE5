import unreal
from pathlib import Path
import copy
import shutil
import os
from dataclasses import dataclass

#Globals
asset_registry_helpers = unreal.AssetRegistryHelpers.get_asset_registry()

@dataclass
class Data:
    files_to_copy = []
    asset_paths = []
    verified_assets = []
    project_dir = unreal.Paths().project_content_dir()
    source_project_content_dir = Path(unreal.Paths().convert_relative_path_to_full(project_dir))
    selected_asset_path = ""
    destination_project_content_dir = ""


    asset_info_data = {'asset_path':None, 'source_asset_path':None, 'destination_asset_path':None, 'is_same':False, 'destination_exists':False, 'destination_is_writable':True}

    def reset(self):
        self.files_to_copy = []
        self.asset_paths = []
        self.verified_assets = []

def collect_files(data, selected_asset_path:None):
    #Get Entry Asset, call this to start the process
    data.selected_asset_path = selected_asset_path
    data.asset = asset_registry_helpers.get_asset_by_object_path(data.selected_asset_path)
    data.asset_paths.append(data.selected_asset_path)
    #Asset Lib
    editor_asset_lib = unreal.EditorAssetLibrary()


    #get top level dependencies
    dependency_options = unreal.AssetRegistryDependencyOptions()
    dependencies = asset_registry_helpers.get_dependencies(data.asset.package_name, dependency_options)

    #Find Material
    for dependency in dependencies:
        asset_data = editor_asset_lib.find_asset_data(dependency)
        if asset_data.asset_class_path.asset_name == "Material":
            data.asset_paths.append(str(dependency))


    #Get Material Dependencies #todo: safeguard
    asset = asset_registry_helpers.get_asset_by_object_path(data.asset_paths[1])
    dependencies = asset_registry_helpers.get_dependencies(asset.package_name, dependency_options)

    data.asset_paths.extend([str(dependency) for dependency in dependencies if not str(dependency).startswith("/Script")])


def check_md5(file_path):
    import hashlib
    hashlib.md5(open(file_path,'rb').read()).hexdigest()
    
def prepare_asset_info(asset_path, source_project_content_dir, destination_project_content_dir):
    """Populates a dictionary with some data around the asset """
    asset_info = copy.deepcopy(Data.asset_info_data)
    asset_info['asset_path'] = asset_path
    asset_info['source_asset_path'] = source_project_content_dir / Path(asset_path.replace("/Game/", "") + ".uasset")
    asset_info['destination_asset_path'] = destination_project_content_dir / Path(asset_path.replace("/Game/", "") + ".uasset")
    if asset_info['destination_asset_path'].exists():
        asset_info['destination_exists'] = True
        if check_md5(asset_info['source_asset_path']) == check_md5(asset_info['destination_asset_path']):
            asset_info['is_same'] = True

        writable = is_file_writable(asset_info['destination_asset_path'])
        print(writable)
        asset_info['destination_is_writable'] = writable

    return asset_info

def prepare_data(data):
    """Prepare Data for the copy Process"""
    for asset_path in data.asset_paths:
        data.verified_assets.append(prepare_asset_info(asset_path, data.source_project_content_dir, data.destination_project_content_dir))

def challenge_user(data):
    """Show the user what files are being copied and which were marked to be skipped"""
    
    skip_list = []
    copy_list = []
    not_writable = []
    
    for asset in data.verified_assets:
        print(asset['asset_path'])
        if asset['destination_exists']:
            if not asset['destination_is_writable']:
                not_writable.append("{}".format(asset['asset_path']))
            elif asset['is_same']:
                skip_list.append("{}".format(asset['asset_path']))
            else:
                copy_list.append("{}".format(asset['asset_path']))
        else:
            copy_list.append("{}".format(asset['asset_path']))
   
    text = "These assets (count {1}) will be mgirated to the destination project {0}\n".format(data.destination_project_content_dir, len(copy_list))

    #Construct Text Sections from skipped, files to copy    
    if copy_list:
        text += "\n\n Files will be copied: \n"
        text += "___________________________________________________________________________________________________________________________________\n"
        text += "\n".join(copy_list)
    if skip_list:
        text += "\n\n\n\n Files are the same, will be skipped: \n"
        text += "___________________________________________________________________________________________________________________________________\n"
        text += "\n".join(skip_list)
    if not_writable:
        text += "\n\n\n\n Files are write protected, will not be copied: \n"
        text += "___________________________________________________________________________________________________________________________________\n"
        text += "\n".join(not_writable)     
    
    copy_files = unreal.EditorDialog.show_message('Files to be Copied', text, unreal.AppMsgType.OK_CANCEL)
    return copy_files

def is_file_writable(file_path):
    try:
        with open(file_path, 'w') as f:
            pass
    except PermissionError:
        return False  # File is write-protected
    return True
    


def copy_files(data, file_list):
    """Copy files from the source to the destination"""
    failed_files = []
    successful_files = []
    for file_dict in file_list:
        src = file_dict["source_asset_path"]
        dest = file_dict["destination_asset_path"]

        
        if file_dict['is_same']:
            print(f"Destination file {dest} exists and is the same, skipping")
        elif src and dest:
            if os.path.exists(src):
                try:
                    shutil.copyfile(src, dest)
                    successful_files.append((src, dest))
                except Exception as e:
                    failed_files.append((src, dest, str(e)))
            else:
                print(f"Source file {src} does not exist.")
        else:
            print(f"Invalid source or destination path: {src}, {dest}")
    


    return successful_files, failed_files