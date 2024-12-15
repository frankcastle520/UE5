# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""
import unreal as ue
import os
import torch
from torch.utils.data import Dataset
import mldeformer.tensorboard_helpers
import numpy as np
import datetime
import time
import pickle
import gc


class TensorUploadDataset(Dataset):
    def __init__(self, inputs, outputs, device):
        self.inputs = inputs
        self.outputs = outputs
        self.device = device

    def __getitem__(self, index):
        return self.inputs[index], self.outputs[index], index

    def __len__(self):
        return len(self.inputs)

    def collate(self, args):
        sample_indices = [a[2] for a in args]
        return (torch.tensor(np.concatenate([a[0][None] for a in args], axis=0), device=self.device),
                torch.tensor(np.concatenate([a[1][None] for a in args], axis=0), device=self.device),
                sample_indices)


def get_available_cuda_devices() -> tuple:
    """Get the list of available devices to use with Cuda.
    
    Returns:
        A tuple, where the first element is the list of device names that can be used with Cuda.
        The second value is the index (into this list) to the current device that is currently set to be used.
    """
    device_list = list()

    if torch.cuda.is_available():
        num_devices = torch.cuda.device_count()
        for device_index in range(num_devices):
            device_name = torch.cuda.get_device_name(device_index)
            device_list.append(device_name)
        current_device_index = torch.cuda.current_device()
    else:
        current_device_index = -1

    return device_list, current_device_index


def find_cuda_device_index(device_name: str) -> int:
    """Find the device index based on the device name.

    Returns:
        Returns the device index to be used as device index for cuda, or -1 when no such device has been found.
    """
    if torch.cuda.is_available():
        num_devices = torch.cuda.device_count()
        for device_index in range(0, num_devices):
            if device_name == torch.cuda.get_device_name(device_index):
                return device_index

    return -1


def update_training_device_list(training_model: ue.MLDeformerTrainingModel):
    device_list, current_device = get_available_cuda_devices()
    device_list.insert(0, 'Cpu')
    current_device = current_device + 1 if len(device_list) > 1 else 0
    training_model.set_device_list(device_list, current_device)


def print_cuda_info():
    if torch.cuda.is_available():
        device_index = torch.cuda.current_device()
        device_name = torch.cuda.get_device_name(device_index)
        print(f'PyTorch Version: {torch.__version__}')
        print(f'CUDA version in PyTorch: {torch.version.cuda}')
        device_list, used_device_index = mldeformer.training_helpers.get_available_cuda_devices()
        print('CUDA available devices:', device_list)
        print(f'CUDA device used: {device_name}')
        print('CUDA device properties:', torch.cuda.get_device_properties(device_index))
        free_mem, total_mem = torch.cuda.mem_get_info(device_index)
        print(f'CUDA device memory stats: {free_mem / (1024 * 1024 * 1024):.2f} gb free of {total_mem / (1024 * 1024 * 1024):.2f} gb total')


def get_training_dir(training_model) -> str:
    """Get the training directory for a given training model.

    Keyword arguments:
        training_model  -- The ML Deformer training model.

    Returns:
        A string containing the full folder path.
    """
    intermediate_dir = ue.Paths.convert_relative_path_to_full(ue.Paths.project_intermediate_dir())
    model_name = str(training_model.get_model().get_class().get_fname())
    training_dir = os.path.join(intermediate_dir, model_name)
    return training_dir


def get_inputs_outputs_filename(training_dir: str):
    """Get the filenames that can be used to make memory mapped files for the input and output training data.
    Please note that this does not create any files or so, it just returns the filenames.

    Keyword arguments:
        training_dir        -- The training directory for this model.

    Returns:
        inputs_filename     -- The filename for the inputs. 
        outputs_filename    -- The filename for the outputs.
    """
    inputs_filename = os.path.join(training_dir, 'inputs.bin')
    outputs_filename = os.path.join(training_dir, 'outputs.bin')
    return inputs_filename, outputs_filename


def prepare_training_folder(training_dir: str, ignore_list: list = None):
    """Prepare the training folder. This will automatically create the folder if it doesn't exist.
    It also remove all existing files inside this folder, except files that are inside some ignore list.
    This function automatically adds the inputs and outputs file as returned by get_inputs_outputs_filename to the 
    ignore list. Also the Tensorboard runs file will be automatically added.

    Keyword arguments:
        training_dir        -- The training directory for this model.
        ignore_list         -- The list of filenames (strings) to ignore.
    """
    inputs_filename, outputs_filename = get_inputs_outputs_filename(training_dir)
    if ignore_list is None:
        ignore_list = list()
    ignore_list.append(inputs_filename)
    ignore_list.append(outputs_filename)
    ignore_list.append(mldeformer.tensorboard_helpers.get_runs_filename(training_dir))
    if os.path.exists(training_dir):
        for f in os.listdir(training_dir):
            filename = os.path.join(training_dir, f)
            if filename not in ignore_list:
                if os.path.isfile(filename):
                    os.remove(filename)

    # If our intermediate training folder doesn't exist yet, create it.
    if not os.path.exists(training_dir):
        os.makedirs(training_dir)


def generate_time_strings(cur_iteration: int, total_num_iterations: int, start_time: float):
    """Generate the elapsed and remaining time strings that we show in the progress window.   

    Keyword arguments:
        cur_iteration           -- The current training iteration.
        total_num_iterations    -- The total number of iterations.
        start_time              -- The start time as returned by time.time().

    Returns:
        passed_time_string          -- The string containing the passed time.
        est_time_remaining_string   -- The string containing the estimated remaining time.   
    """
    iterations_remaining = total_num_iterations - cur_iteration
    passed_time = time.time() - start_time
    avg_iteration_time = passed_time / (cur_iteration + 1)
    est_time_remaining = iterations_remaining * avg_iteration_time
    est_time_remaining_string = str(datetime.timedelta(seconds=int(est_time_remaining)))
    passed_time_string = str(datetime.timedelta(seconds=int(passed_time)))
    return passed_time_string, est_time_remaining_string


def generate_inputs_outputs(training_model,
                            inputs_filename: str,
                            outputs_filename: str,
                            data_start_time: float,
                            include_curves: bool,
                            num_inputs_per_curve: int = 1):
    """Generate the training input and output data, stored in memory mapped files.
    This will create the memory mapped files, sample the inputs and outputs, and save the memory mapped files.
    A SlowTask dialog will be created during sampling of all the input samples.
    
    Keyword arguments:
        training_model       -- The ML Deformer training model.
        inputs_filename      -- The filename to use for the memory mapped file that contains the training inputs.
        outputs_filename     -- The filename to use for the memory mapped file that contains the training outputs.
        data_start_time      -- The start time when we started sampling.
        include_curves       -- Should we include sampling curves?
    """
    print('Sampling training data frames')
    training_model.set_num_floats_per_curve(num_inputs_per_curve)
    num_samples = training_model.num_samples()
    num_bone_values = 6 * training_model.get_number_sample_transforms()  # 2 columns of 3x3 rotation matrix per bone.
    num_curve_values = num_inputs_per_curve * training_model.get_number_sample_curves() if include_curves else 0
    num_delta_values = 3 * training_model.get_number_sample_deltas()  # xyz per vertex delta.

    # Create inputs/outputs. Memory map inputs and outputs.
    num_inputs = num_bone_values + num_curve_values
    inputs = np.memmap(inputs_filename, dtype=np.float32, mode='w+', shape=(num_samples, num_inputs))
    outputs = np.memmap(outputs_filename, dtype=np.float32, mode='w+', shape=(num_samples, num_delta_values))

    # Iterate over all sample frames.
    with ue.ScopedSlowTask(num_samples, 'Sampling Frames') as sampling_task:
        sampling_task.make_dialog(True)
        for i in range(num_samples):
            # Stop if the user has pressed Cancel in the UI.
            # The 'CannotUse' makes sure the user doesn't get offered to use the partially trained network, because
            # at this stage we didn't even create and train a network yet.
            if sampling_task.should_cancel():
                raise GeneratorExit('CannotUse')

            # Set the sample.
            training_model.next_sample()

            # Copy inputs.
            inputs[i, :num_bone_values] = training_model.sample_bone_rotations
            if include_curves:
                inputs[i, num_bone_values:] = training_model.sample_curve_values

            # Copy outputs
            outputs[i] = training_model.sample_deltas

            # Calculate passed and estimated time and report progress.
            passed_time_string, est_time_remaining_string = generate_time_strings(i, num_samples, data_start_time)
            sampling_task.enter_progress_frame(
                1,
                f'Sampling frame {i + 1:6d} of {num_samples} - '
                f'Time: {passed_time_string} - Left: {est_time_remaining_string}')

            if (i % 100 == 0 and i != 0) or (i == num_samples - 1):
                print(f'Sampled {i + 1 : 6d} / {num_samples} training frames')

    # Save the generated inputs and outputs.
    inputs._mmap.close()
    outputs._mmap.close()
    print('Finished sampling training data frames')


def save_mask_index_per_sample_array(mask_array: np.array, filename: str):
    """Save a cached version of the array that holds the mask indices for each sample.

       Keyword arguments:
            filename -- The filename of the cache file that we save the array to.
    """
    print(f'Saving cached mask index per sample array to file: {filename}')
    try:
        with open(filename, 'wb') as mask_index_cache_file:
            pickle.dump(mask_array, mask_index_cache_file)
    except IOError:
        print('Failed to save cached mask index per sample file')


def load_mask_index_per_sample_array(filename: str) -> np.array:
    """Load a cached version of the array that holds the mask indices for each sample.

       Keyword arguments:
            filename -- The filename of the cache file that was passed to the save_mask_index_per_sample_array() function.
    """
    print(f'Loading cached mask index per sample array from file: {filename}')
    try:
        with open(filename, 'rb') as mask_index_cache_file:
            return pickle.load(mask_index_cache_file)
    except IOError:
        print('Failed to load cached mask index per sample file')
        return np.empty([0])


def verify_tensor(name: str, tensor: torch.Tensor):
    if torch.isnan(tensor).any():
        print(f'Tensor \'{name}\' has NaN values!')

    if torch.isinf(tensor).any():
        print(f'Tensor \'{name}\' has Inf values!')


def print_tensor_devices(module, indent=0):
    for name, param in module.named_parameters():
        print(' ' * indent + f'{name}: {param.device}')

    for name, buffer in module.named_buffers():
        print(' ' * indent + f'{name}: {buffer.device}')

    for name, submodule in module.named_children():
        print(' ' * indent + f'Module: {name}')
        print_tensor_devices(submodule, indent + 2)


def print_all_tensor_devices():
    # Find all pytorch tensor objects currently inside the garbage collector.
    objects = gc.get_objects()
    tensors = [obj for obj in objects if isinstance(obj, torch.Tensor)]

    for tensor in tensors:
        print(f'Tensor with shape {tensor.shape} is on device: {tensor.device}')


def print_module_devices():
    # Collect all objects
    all_objects = gc.get_objects()

    # Filter out objects that are instances of torch.nn.Module
    model_objects = [obj for obj in all_objects if isinstance(obj, torch.nn.Module)]

    # Iterate through each model and print the device of its parameters
    for model in model_objects:
        devices = set(param.device for param in model.parameters())
        print(f"Module: {model.__class__.__name__}")
        for device in devices:
            print(f"  Device: {device}")
