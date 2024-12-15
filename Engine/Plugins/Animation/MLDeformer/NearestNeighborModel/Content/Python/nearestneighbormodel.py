# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

import os
from os.path import join, isdir

os.environ['CUDA_LAUNCH_BLOCKING'] = '1'
import copy
import math
import traceback
import logging
import time
import unreal as ue
import numpy as np
import torch
import torch.nn as nn
import datetime
from torch.utils.data import DataLoader
from importlib import reload
import nne_runtime_basic_cpu

reload(nne_runtime_basic_cpu)
from sklearn.decomposition import PCA
from sklearn.cluster import KMeans
import mldeformer.training_helpers

tensorboard_exist = True
try:
    from torch.utils.tensorboard import SummaryWriter
except ImportError as e:
    tensorboard_exist = False

_success = 0
_error = 1
_warning = 2


def get_acc_arr(in_arr):
    out_arr = [0 for i in range(len(in_arr) + 1)]
    for i in range(len(in_arr)):
        out_arr[i + 1] = out_arr[i] + in_arr[i]
    return out_arr


def files_exist(files):
    return all(os.path.exists(file) for file in files)


# requires num_anims, acc_anim_frames, num_anim_frames in param
def get_anim_and_frame(param, i):
    for anim_id in range(param.num_anims):
        if i < param.acc_anim_frames[anim_id + 1]:
            return anim_id, i - param.acc_anim_frames[anim_id]
    return param.num_anims - 1, param.num_anim_frames[-1] - 1


class Section:
    def __init__(self, ue_section):
        self.num_vts = ue_section.num_vertices
        vertex_map = np.empty(self.num_vts, dtype=np.int32)
        vertex_map[:] = ue_section.get_vertex_map()
        self.vertex_map = vertex_map
        self.num_basis = ue_section.get_num_basis()
        self.asset_num_neighbors = ue_section.get_asset_num_neighbors()


class TrainParam:
    def __init__(self, api):
        self.api = api
        self.use_debug_mode = False

    def init_debug_param(self):
        self.skip_training = False
        self.skip_training_except = GeneratorExit('Unusable')

        # for debugging
        # self.skip_training = True
        # self.skip_training_except = GeneratorExit('Usable') 

        self.use_debug_mode = False

        if self.use_debug_mode:
            self.inputs_dir = f'{self.training_dir}/inputs'
            self.outputs_dir = f'{self.training_dir}/UEOutputs'
            if not isdir(self.inputs_dir):
                os.makedirs(self.inputs_dir)
            if not isdir(self.outputs_dir):
                os.makedirs(self.outputs_dir)
        self.debug_max_frames = 100

    def init_common_param(self):
        self.model = self.api.get_nearest_neighbor_model()
        assert (self.model is not None)
        self.training_dir = self.model.get_model_dir()
        if not os.path.exists(self.training_dir):
            os.makedirs(self.training_dir)
        self.use_pca = self.model.does_use_pca()

    def init_network_param(self):
        self.num_bone_values = 3 * self.api.get_number_sample_transforms()  # x, y, z of the quaternions
        self.num_curve_values = self.api.get_number_sample_curves()
        self.num_inputs = self.num_bone_values + self.num_curve_values
        self.num_sample_deltas = self.api.get_number_sample_deltas()

    def init_section_param(self):
        self.num_sections = self.model.get_num_sections()
        self.sections = [Section(self.model.get_section_ptr(section_id)) for section_id in range(self.num_sections)]

    def init_file_cache_param(self):
        self.use_file_cache = self.model.use_file_cache
        self.recompute_deltas = not self.model.use_file_cache or not files_exist(self.model.get_cached_deltas_paths())
        self.recompute_pca = not self.model.use_file_cache or not files_exist(self.model.get_cached_pca_paths())
        self.train_network = not self.model.use_file_cache or not files_exist(self.model.get_cached_network_paths())

    def init_delta_param(self):
        self.delta_dtype = np.float32
        self.num_samples = self.api.num_samples()
        self.num_sum_deltas = sum(section.num_vts for section in self.sections)
        self.acc_section_num_vts = get_acc_arr([section.num_vts for section in self.sections])
        self.deltas_path = f'{self.training_dir}/deltas.bin'
        self.inputs_path = f'{self.training_dir}/inputs.npy'
        if self.use_pca:
            self.outputs_path = f'{self.training_dir}/output_coeffs.npy'
        else:
            self.outputs_path = self.deltas_path

    def init_network_train_param(self):
        self.batch_size = self.model.get_batch_size()
        self.num_iters = self.model.get_num_iterations()

        self.lr = self.model.get_learning_rate()
        self.device = get_device(self.model)
        self.early_stop_epochs = self.model.get_early_stop_epochs()
        if self.use_pca:
            self.regularization_factor = 0

            def loss_fn(X, Y):
                return torch.nn.functional.mse_loss(X, Y, reduction='sum') / self.num_sum_deltas / len(X)

            self.loss_fn = loss_fn
            self.use_scheduler = False
        else:
            self.regularization_factor = self.model.get_regularization_factor()
            smooth_loss_beta = self.model.get_smooth_loss_beta()
            self.loss_fn = nn.SmoothL1Loss(beta=smooth_loss_beta)
            self.use_scheduler = True
        self.store_data_on_device = True
        self.use_val = self.use_pca

    def init_train_param(self):
        self.init_common_param()
        self.init_network_param()
        self.init_section_param()
        self.init_delta_param()
        self.init_file_cache_param()
        self.init_network_train_param()
        self.init_debug_param()

    def init_update_neighbor_param(self):
        self.init_common_param()
        self.init_section_param()
        self.init_network_param()

        self.delta_dtype = np.float32

    def init_kmeans_param(self, data):
        self.data = data
        model = self.api.get_nearest_neighbor_model()
        self.model = model
        if model is None:
            raise RuntimeError('Nearest neighbor model is needed for kmeans')

        self.num_anims = len(data.inputs)
        self.num_anim_frames = [self.api.get_num_frames_anim_sequence(input_data.poses) for input_data in data.inputs]
        self.acc_anim_frames = [0 for i in range(self.num_anims + 1)]
        for anim_id in range(self.num_anims):
            self.acc_anim_frames[anim_id + 1] = self.acc_anim_frames[anim_id] + self.num_anim_frames[anim_id]

        self.num_bone_values = 3 * self.api.get_number_sample_transforms()  # x, y, z of the quaternions
        self.num_curve_values = self.api.get_number_sample_curves()
        self.num_inputs = self.num_bone_values + self.num_curve_values
        self.use_pca = model.does_use_pca()

    def init_neighbor_stats_param(self, section_id):
        self.init_common_param()
        self.init_section_param()

        self.section_id = section_id
        self.num_bone_values = 3 * self.api.get_number_sample_transforms()  # x, y, z of the quaternions
        self.num_curve_values = self.api.get_number_sample_curves()
        self.num_inputs = self.num_bone_values + self.num_curve_values

    def get_section_ptr_range(self, section_id):
        return range(self.acc_section_num_vts[section_id] * 3, self.acc_section_num_vts[section_id + 1] * 3)


# ====================================== Network ======================================
class MLPPCA(nn.Module):
    def __init__(self, dims, inputs_mean, inputs_std, outputs_mean, outputs_std):
        super(MLPPCA, self).__init__()
        layers = []
        for i in range(len(dims) - 2):
            layers += [
                nn.Linear(dims[i], dims[i + 1]),
                nn.BatchNorm1d(dims[i + 1]),
                nn.PReLU()
            ]
        layers.append(nn.Linear(dims[-2], dims[-1]))
        self.model = nn.Sequential(*layers)

        self.register_buffer('inputs_mean', inputs_mean[None, :])
        self.register_buffer('inputs_std', inputs_std[None, :] + 1e-6)
        self.register_buffer('outputs_mean', outputs_mean[None, :])
        self.register_buffer('outputs_std', outputs_std[None, :] + 1e-6)

    def forward(self, x):
        x = (x - self.inputs_mean) / self.inputs_std
        return self.model(x) * self.outputs_std + self.outputs_mean


class MLP(nn.Module):
    def __init__(self, dims):
        super(MLP, self).__init__()
        layers = []
        for i in range(len(dims) - 1):
            layers += [
                nn.Linear(dims[i], dims[i + 1]),
                nn.ELU()
            ]

        self.model = nn.Sequential(*layers)
        self.inputs_mean = None
        self.inputs_std = None
        self.outputs_mean = None
        self.outputs_std = None

    def forward(self, x):
        if self.inputs_mean is not None and self.inputs_std is not None:
            x = (x - self.inputs_mean) / self.inputs_std
        x = self.model(x)
        if self.outputs_mean is not None and self.outputs_std is not None:
            x = x * self.outputs_std + self.outputs_mean
        return x


class Corrective(nn.Module):
    def __init__(self, dims, inputs_mean, inputs_std, outputs_mean, outputs_std):
        super(Corrective, self).__init__()
        if len(dims) < 2:
            raise ValueError('dims should have at least 2 elements')
        self.model = MLP(dims[:-1])
        self.morph_target_matrix = nn.parameter.Parameter(
            torch.zeros((dims[-1], dims[-2])))
        nn.init.kaiming_uniform_(self.morph_target_matrix, a=math.sqrt(5))

        self.coeff_scales = None
        self.register_buffer('inputs_mean', inputs_mean[None, :])
        self.register_buffer('inputs_std', inputs_std[None, :] + 1e-6)
        self.register_buffer('outputs_mean', outputs_mean[None, :])
        self.register_buffer('outputs_std', outputs_std[None, :] + 1e-6)

    def forward(self, x):
        x = (x - self.inputs_mean) / self.inputs_std
        x = self.model(x)
        if self.coeff_scales is not None:
            x = x * self.coeff_scales
        x = torch.matmul(self.morph_target_matrix, x.unsqueeze(-1)).squeeze(-1)
        return x * self.outputs_std + self.outputs_mean


def get_dims(param):
    model = param.model
    num_inputs = param.num_inputs
    hidden_dims = model.get_hidden_layer_dims()
    num_outputs = param.model.get_total_num_basis()
    dims = [num_inputs] + list(hidden_dims) + [num_outputs]
    if not param.use_pca:
        dims.append(param.num_sum_deltas * 3)
    return dims


def create_empty_network(param):
    model = param.model

    dims = get_dims(param)
    if param.use_debug_mode:
        out_path = f'{param.training_dir}/dims.txt'
        print('write to', out_path)
        np.savetxt(out_path, np.array(dims), fmt='%d')
    num_inputs = dims[0]
    num_outputs = dims[-1]
    inputs_mean = torch.zeros(num_inputs)
    inputs_std = torch.zeros(num_inputs)
    outputs_mean = torch.zeros(num_outputs)
    outputs_std = torch.zeros(num_outputs)

    if param.use_pca:
        network = MLPPCA(dims, inputs_mean, inputs_std, outputs_mean, outputs_std)
    else:
        network = Corrective(dims, inputs_mean, inputs_std, outputs_mean, outputs_std)

    return network


def load_torch_network(param, path):
    network = create_empty_network(param)
    network.load_state_dict(torch.load(path))

    return network


# ====================================== Utils ======================================
seed = 1234


def rand_seed():
    global seed
    np.random.seed(seed)
    torch.random.manual_seed(seed)


def generate_time_strings(cur_iteration, total_num_iterations, start_time):
    iterations_remaining = total_num_iterations - cur_iteration
    passed_time = time.time() - start_time
    avg_iteration_time = passed_time / (cur_iteration + 1)
    est_time_remaining = iterations_remaining * avg_iteration_time
    est_time_remaining_string = str(datetime.timedelta(seconds=int(est_time_remaining)))
    passed_time_string = str(datetime.timedelta(seconds=int(passed_time)))
    return passed_time_string, est_time_remaining_string


def get_device(model):
    training_device = model.get_training_device()
    device_index = mldeformer.training_helpers.find_cuda_device_index(device_name=training_device)
    if torch.cuda.is_available() and device_index != -1:
        device = 'cuda'
        torch.cuda.set_device(device_index)
        mldeformer.training_helpers.print_cuda_info()
    else:
        device = 'cpu'
        print(f'Using the CPU for training')
    return device


def has_error(return_code):
    return (return_code & 1) != 0


class EvalNetwork:
    def __init__(self, param):
        self.api = param.api
        self.instance = param.api.create_model_instance()

    def is_valid(self):
        return self.instance is not None

    def forward(self, x):
        x = self.instance.eval(x.tolist())
        return np.array(x)

    def __del__(self):
        self.api.destroy_model_instance(self.instance)


# ====================================== Update nearest neighbor data ======================================
def sample_neighbor_data(param):
    num_sections = param.num_sections
    api = param.api
    inputs = [None for section_id in range(num_sections)]
    deltas = [None for section_id in range(num_sections)]
    delta_buffer = np.empty(param.num_sample_deltas * 3, dtype=param.delta_dtype)

    total_tasks = sum([section.asset_num_neighbors for section in param.sections])
    with ue.ScopedSlowTask(total_tasks, "Sample nearest neighbor data") as update_task:
        update_task.make_dialog(True)
        for section_id in range(num_sections):
            if update_task.should_cancel():
                raise GeneratorExit('Aborted')

            if not api.set_custom_sampler_data_from_section(section_id):
                raise RuntimeError('SetSamplerData failed')

            section = param.sections[section_id]
            num_samples = section.asset_num_neighbors
            num_vts = section.num_vts

            section_inputs = np.empty((num_samples, param.num_inputs), dtype=np.float32)
            section_deltas = np.empty((num_samples, num_vts * 3), dtype=np.float32)

            for sample_id in range(num_samples):
                if update_task.should_cancel():
                    raise GeneratorExit('Aborted')

                api.custom_sample(sample_id)

                delta_buffer[:] = api.custom_sampler_deltas
                section_delta = delta_buffer.reshape((-1, 3))[param.sections[section_id].vertex_map]
                section_deltas[sample_id, :] = section_delta.reshape(-1)
                section_inputs[sample_id, :] = api.custom_sampler_bone_rotations

                update_task.enter_progress_frame(1,
                                                 f'UpdateNearestNeighborData: sampling part {section_id}, frame {sample_id + 1:6d} of {num_samples:6d}')

            inputs[section_id] = section_inputs
            deltas[section_id] = section_deltas

    return inputs, deltas


def update_neighbor_offsets(param, eval_network, inputs, deltas):
    num_sections = param.num_sections
    model = param.model

    total_tasks = sum(section.asset_num_neighbors for section in param.sections)
    with ue.ScopedSlowTask(total_tasks, "Update nearest neighbor offsets") as update_task:
        update_task.make_dialog(True)

        for section_id in range(num_sections):
            if update_task.should_cancel():
                raise GeneratorExit('Aborted')

            section = param.sections[section_id]
            section_inputs = inputs[section_id]
            section_deltas = deltas[section_id]
            num_samples = section.asset_num_neighbors
            num_vts = section.num_vts
            num_basis = section.num_basis
            coeff_start = (model.get_pca_coeff_starts()[section_id] if param.use_pca
                           else 0)

            ue_section = model.get_section_ptr(section_id)

            vertex_mean = np.empty(num_vts * 3, dtype=np.float32)
            vertex_mean[:] = ue_section.get_vertex_mean()

            basis = np.empty(num_basis * num_vts * 3, dtype=np.float32)
            ue_basis = ue_section.get_basis()
            basis[:] = ue_section.get_basis()
            basis = basis.reshape((num_basis, num_vts * 3))

            coeffs = np.empty((num_samples, num_basis), dtype=np.float32)
            remaining_offsets = np.empty((num_samples, num_vts * 3), dtype=np.float32)

            for sample_id in range(num_samples):
                if update_task.should_cancel():
                    raise GeneratorExit('Aborted')

                sample_coeff = eval_network.forward(section_inputs[sample_id])
                sample_coeff = sample_coeff[coeff_start: coeff_start + num_basis]
                coeffs[sample_id, :] = sample_coeff
                remaining_offsets[sample_id, :] = section_deltas[sample_id] - sample_coeff.dot(basis) - vertex_mean

                update_task.enter_progress_frame(1,
                                                 f'UpdateNearestNeighborData: updating part {section_id}: frame {sample_id + 1:6d} of {num_samples:6d}')

            neighbor_coeffs = coeffs.reshape(-1).tolist()
            neighbor_offsets = remaining_offsets.reshape(-1).tolist()
            ue_section.set_neighbor_data(neighbor_coeffs, neighbor_offsets)


def update_nearest_neighbor_data(api):
    return_code = _success
    try:
        param = TrainParam(api)
        param.init_update_neighbor_param()
        inputs, deltas = sample_neighbor_data(param)
        eval_network = EvalNetwork(param)
        if not eval_network.is_valid():
            ue.log_error('Network is empty. Please train the network first.')
            return _error
        update_neighbor_offsets(param, eval_network, inputs, deltas)

    except GeneratorExit as e:
        if str(e) == 'Aborted':
            ue.log_warning('Nearest neighbor update aborted by user')
            return_code = _error
        else:
            logging.error(e)
            logging.error(traceback.format_exc())
            return_code = _error
    except Exception as e:
        logging.error(e)
        logging.error(traceback.format_exc())
        return_code = _error

    return return_code


# ====================================== Kmeans ======================================
def find_nearest_neighbor(X, coeff):
    d2 = (X - np.expand_dims(coeff, 0)) ** 2
    d = np.sum(d2, axis=1)
    return np.argmin(d)


def save_dims(param):
    model = param.model

    dims = get_dims(param)
    if param.use_debug_mode:
        out_path = f'{param.training_dir}/dims.txt'
        print('write to', out_path)
        np.savetxt(out_path, np.array(dims), fmt='%d')


def get_anim_coeffs(param, section_id, anim, eval_network):
    model = param.model
    section_coeffs = []

    if param.use_debug_mode:
        save_dims(param)

    param.api.set_custom_sampler_data(anim)

    num_frames = param.api.get_num_frames_anim_sequence(anim)
    with ue.ScopedSlowTask(num_frames, "Sampling") as sample_task:
        sample_task.make_dialog(True)

        section = model.get_section_ptr(section_id)
        num_basis = section.get_num_basis()
        if num_basis < 1:
            raise RuntimeError('Section has zero basis')
        coeff_start = (model.get_pca_coeff_starts()[section_id] if param.use_pca
                       else 0)
        for frame in range(num_frames):
            if sample_task.should_cancel():
                raise GeneratorExit('Aborted')
            sample_exists = param.api.custom_sample(frame)
            if not sample_exists:
                raise RuntimeError(f'Failed to sample frame {frame} of animation')

            inputs = np.zeros(param.num_inputs, dtype=np.float32)
            inputs[:] = param.api.custom_sampler_bone_rotations

            sample_coeff = eval_network.forward(inputs)
            section_coeffs.append(sample_coeff[coeff_start: coeff_start + num_basis])

            sample_task.enter_progress_frame(1, f'Sampling frame {frame + 1:6d} of {num_frames:6d}')

            if param.use_debug_mode:
                out_path = f'{param.inputs_dir}/{frame:08d}.npy'
                print('write to', out_path)
                np.save(out_path, inputs)
                out_path = f'{param.outputs_dir}/{frame:08d}.npy'
                print('write to', out_path)
                np.save(out_path, sample_coeff)
                if frame > param.debug_max_frames:
                    break

        return section_coeffs


def sample_kmeans_data(param, eval_network):
    data = param.data
    section_coeffs = []
    must_include = []
    acc_n_frames = 0
    for i, input_data in enumerate(data.inputs):
        coeffs = get_anim_coeffs(param, data.section_index, input_data.poses, eval_network)
        n_frames = len(coeffs)
        section_coeffs.append(coeffs)

        for frame in input_data.must_include_frames:
            if frame < 0 or frame >= n_frames:
                print(f'Input {i}, MustIncludeFrame {frame} is out of range [0, {n_frames - 1}]. Ignored.')
                continue
            must_include.append(acc_n_frames + frame)
        acc_n_frames += n_frames

    return np.concatenate(section_coeffs, axis=0), must_include


def initialize_kmeans_centers(X, n_clusters):
    n, d = X.shape
    centers = np.empty((n_clusters, d))

    idx = np.random.choice(n)
    centers[0] = X[idx]
    distance2 = np.sum((X - centers[0]) ** 2, axis=1)

    for i in range(1, n_clusters):
        d = np.sum((X - centers[i - 1]) ** 2, axis=1)
        distance2 = np.minimum(distance2, np.sum((X - centers[i - 1]) ** 2, axis=1))

        probabilities = distance2 / distance2.sum()
        idx = np.random.choice(n, p=probabilities)
        centers[i] = X[idx]

    return centers


def must_include_kmeans(X, n_clusters, max_iter, must_include):
    n_must_include = len(must_include)
    if n_clusters < n_must_include:
        raise ValueError('n_clusters must be greater than or equal to must_include')
    if len(X) < n_clusters:
        raise ValueError('X must have at least n_clusters samples')

    d = X.shape[1]
    centers = np.empty((n_clusters, d), dtype=np.float32)
    centers[:n_must_include] = X[must_include]

    n_free_clusters = n_clusters - n_must_include
    if n_free_clusters == 0:
        return centers

    total_indices = np.arange(len(X), dtype=np.int32)
    free_indices = np.delete(total_indices, must_include)
    X_free = X[free_indices]

    centers[n_must_include:] = initialize_kmeans_centers(X_free, n_free_clusters)

    if n_free_clusters == n_clusters:
        return centers

    labels = None
    for i in range(max_iter):
        # (a - b)^2 = a^2 - 2ab + b^2
        distances = - 2 * np.dot(X_free, centers.T) + np.sum(X_free ** 2, axis=1)[:, None] + np.sum(centers ** 2, axis=1)[None, :]
        new_labels = np.argmin(distances, axis=1)
        if labels is not None and np.array_equal(labels, new_labels):
            break

        new_centers = np.zeros((n_clusters, d), dtype=np.float32)
        np.add.at(new_centers, new_labels, X_free)
        counts = np.bincount(new_labels, minlength=n_clusters)
        free_counts = counts[n_must_include:]
        free_counts = np.clip(free_counts, 1, None)
        new_centers[n_must_include:] /= free_counts[:, None]
        new_centers[:n_must_include] = centers[:n_must_include]
        centers = new_centers
        labels = new_labels

    return centers


def kmeans_and_find_poses(param, section_coeffs, must_include=[]):
    rand_seed()
    results = []
    with ue.ScopedSlowTask(1, "Running KMeans (this may take a while)") as kmeans_task:
        kmeans_task.make_dialog(True)
        n_clusters = param.data.num_clusters
        centers = must_include_kmeans(section_coeffs, n_clusters, 10 * n_clusters, must_include)

        anim_ids = []
        frames = []
        for cluster_id in range(n_clusters):
            center = centers[cluster_id]
            acc_id = find_nearest_neighbor(section_coeffs, center)
            anim_id, frame = get_anim_and_frame(param, acc_id)
            anim_ids.append(anim_id)
            frames.append(frame)

        results = [pair for pair in zip(anim_ids, frames)]
    return results


def get_model_skeleton(model):
    return model.skeletal_mesh.get_skeleton()


def save_kmeans_anim(skeleton, results, inputs, out_anim, out_cache=None):
    if skeleton is None or not isinstance(skeleton, ue.Skeleton):
        return False
    if out_anim is None or not isinstance(out_anim, ue.AnimSequence):
        return False
    if len(inputs) == 0:
        return False

    anim_stream = ue.NearestNeighborAnimStream()
    anim_stream.init(skeleton)
    if not anim_stream.is_valid():
        return False

    extract_cache = out_cache is not None
    if extract_cache:
        cache_stream = ue.NearestNeighborGeometryCacheStream()
        cache_stream.init(inputs[0].cache)
        if not cache_stream.is_valid():
            print('cache stream is not valid')
            return False

    for anim_id, frame in results:
        if not anim_stream.append_frames(inputs[anim_id].poses, [int(frame)]):
            return False
        if extract_cache:
            if not cache_stream.append_frames(inputs[anim_id].cache, [int(frame)]):
                return False

    if not anim_stream.to_anim(out_anim):
        return False
    if extract_cache:
        if not cache_stream.to_geometry_cache(out_cache):
            return False
    return True


def kmeans_cluster_poses(api, data):
    return_code = _success
    results = []
    try:
        param = TrainParam(api)
        param.init_kmeans_param(data)
        assert (param.model is not None)

        num_sections = param.model.get_num_sections()
        if data.section_index < 0 or data.section_index >= num_sections:
            ue.log_error(f'Section index {section_index} is out of range [0, {num_sections - 1}].')
            return _error

        if len(data.inputs) == 0:
            ue.log_error(f'Input poses is empty. Please add at lease one sequence.')
            return _error

        if data.extracted_poses is None:
            ue.log_error(f'Extracted poses is empty. Please select an asset to write to.')
            return _error

        if data.num_clusters <= 0:
            ue.log_error(f'Number of clusters must be positive.')
            return _error

        extracted_cache = data.extracted_cache
        if data.extract_geometry_cache:
            for i, input_data in enumerate(data.inputs):
                anim_frames = api.get_num_frames_anim_sequence(input_data.poses)
                cache_frames = api.get_num_frames_geometry_cache(input_data.cache)
                if anim_frames != cache_frames:
                    ue.log_error(f'Frame mismatch. Cache {i} has {cache_frames} frames, but anim {i} has {anim_frames} frames.')
                    return _error

            if data.extracted_cache is None:
                ue.log_error(f'Extracted cache is empty. Please select an asset to write to.')
                return _error

            extracted_cache = data.extracted_cache

        if not param.model.is_ready_for_inference():
            ue.log_error(f'Model is not ready for inference. Please udpate the model first.')
            return _error

        eval_network = EvalNetwork(param)
        if not eval_network.is_valid():
            ue.log_error('Network is empty. Please train the network first.')
            return _error

        section_coeffs, must_include = sample_kmeans_data(param, eval_network)
        results = kmeans_and_find_poses(param, section_coeffs, must_include)
        success = save_kmeans_anim(api.get_model_skeleton(param.model), results, data.inputs, data.extracted_poses, extracted_cache)
        if not success:
            ue.log_error('Failed to save kmeans animation')
            return _error
    except GeneratorExit as e:
        if str(e) == 'Aborted':
            ue.log_warning('Kmeans aborted by user')
            return_code = 2
        else:
            logging.error(e)
            logging.error(traceback.format_exc())
            return_code = 1
    except Exception as e:
        logging.error(e)
        logging.error(traceback.format_exc())
        return_code = 1

    if return_code == 0:
        print('kmeans results:')
        print(results)
    return return_code


# ==============================Get Neighbor Stats ================================
def compute_neighbor_stats(neighbor_ids, num_neighbors):
    counts = np.zeros(num_neighbors, dtype=np.int32)
    indices = [[] for i in range(num_neighbors)]
    for i, neighbor_id in enumerate(neighbor_ids):
        counts[neighbor_id] += 1
        indices[neighbor_id].append(i)
    return counts, indices


def find_similar_poses(src_poses, tgt_poses, num_to_keep=5):
    similar_poses = np.empty((len(src_poses), num_to_keep), dtype=np.int32)
    for i, src_pose in enumerate(src_poses):
        d2 = (tgt_poses - np.expand_dims(src_pose, 0)) ** 2
        d = np.sum(d2, axis=1)
        similar_poses[i] = np.argsort(d)[1:num_to_keep + 1]
    return similar_poses


def get_short_indices_str(indices, num=5):
    if len(indices) <= num:
        return f"[{','.join([str(index) for index in indices])}]"
    else:
        short_indices = [indices[i * len(indices) // num] for i in range(num)]
        return f"[{','.join([str(index) for index in short_indices])} ,...]"


def print_neighbor_stats(counts, indices, similar_poses):
    sorted_indices = np.argsort(counts)
    for i in sorted_indices:
        print(f'Index: {i}, similar poses: {similar_poses[i]}, occurence: {counts[i]}, frames: {get_short_indices_str(indices[i])}')


def get_neighbor_stats(api, data):
    try:
        param = TrainParam(api)
        section_id = data.section_index
        param.init_neighbor_stats_param(section_id)
        if section_id < 0 or section_id >= param.num_sections:
            ue.log_error(f'Section index {section_id} is out of range [0, {param.num_sections - 1}].')
            return False
        if not param.model.is_ready_for_inference():
            ue.log_error('Model is not ready for inference. Please udpate the model first.')
            return False
        eval_network = EvalNetwork(param)
        if not eval_network.is_valid():
            ue.log_error('Network is empty. Please train the network first.')
            return False
        anim = data.test_anim
        if anim is None:
            ue.log_error('Please select an animation to test')
            return False
        num_neighbors = param.sections[section_id].asset_num_neighbors
        if num_neighbors <= 0:
            ue.log_error(f'Section {section_id} does not have neighbors. Please select neighbor data and click update.')
            return False

        num_basis = param.sections[section_id].num_basis
        pred_coeffs = get_anim_coeffs(param, section_id, anim, eval_network)
        if len(pred_coeffs) == 0:
            ue.log_error('Test anim has no frames. Please select a valid animation')
            return False
        if len(pred_coeffs[0]) != num_basis:
            raise RuntimeError('Predicted coeffs mismatch')

        ue_section = param.model.get_section_ptr(section_id)
        neighbor_coeffs = np.array(ue_section.get_asset_neighbor_coeffs(), dtype=np.float32)
        neighbor_coeffs = neighbor_coeffs.reshape((num_neighbors, -1))
        if neighbor_coeffs.shape[1] != num_basis:
            raise RuntimeError('Neighbor coeffs mismatch')

        neighbor_ids = [find_nearest_neighbor(neighbor_coeffs, coeffs) for coeffs in pred_coeffs]
        counts, indices = compute_neighbor_stats(neighbor_ids, num_neighbors)
        similar_poses = find_similar_poses(neighbor_coeffs, neighbor_coeffs)
        print_neighbor_stats(counts, indices, similar_poses)
    except GeneratorExit as e:
        if str(e) == 'Aborted':
            ue.log_warning('Neighbor stats aborted by user')
    except Exception as e:
        logging.error(e)
        logging.error(traceback.format_exc())
    return False


# ====================================== run ======================================
def save_tpose_data(param):
    unskinned = param.api.get_unskinned_vertex_positions()
    unskinned = np.array(unskinned).reshape((-1, 3))
    out_path = f'{param.training_dir}/unskinned.npy'
    print('write to', out_path)
    np.save(out_path, unskinned)

    faces = param.api.get_mesh_index_buffer()
    faces = np.array(faces).reshape((-1, 3))
    out_path = f'{param.training_dir}/topo.npy'
    print('write to', out_path)
    np.save(out_path, faces)

    for section_id in range(param.num_sections):
        out_path = f'{param.training_dir}/vertex_map_{section_id}.txt'
        print('write to', out_path)
        np.savetxt(out_path, param.sections[section_id].get_vertex_map())


# ====================================== deltas ======================================
def compute_and_save_deltas(param):
    api = param.api
    deltas = np.memmap(param.deltas_path, dtype=param.delta_dtype, mode='w+', shape=(param.num_samples, param.num_sum_deltas * 3))
    inputs = np.empty([param.num_samples, param.num_inputs], dtype=np.float32)

    delta_buffer = np.empty(param.num_sample_deltas * 3, dtype=param.delta_dtype)
    data_start_time = time.time()
    with ue.ScopedSlowTask(param.num_samples, "Sampling Frames") as sampling_task:
        sampling_task.make_dialog(True)
        for i in range(param.num_samples):
            # Stop if the user has pressed Cancel in the UI
            if sampling_task.should_cancel():
                raise GeneratorExit('CannotUse')

            # Set the sample
            api.next_sample()

            # Copy inputs
            inputs[i, :param.num_bone_values] = api.sample_bone_rotations
            inputs[i, param.num_bone_values:] = api.sample_curve_values

            # Copy outputs
            delta_buffer[:] = api.sample_deltas
            full_delta = delta_buffer.reshape((-1, 3))
            for section_id in range(param.num_sections):
                section_delta = full_delta[param.sections[section_id].vertex_map]
                section_range = param.get_section_ptr_range(section_id)
                deltas[i, section_range] = section_delta.reshape(-1)

            # Calculate passed and estimated time and report progress
            passed_time_string, est_time_remaining_string = generate_time_strings(i, param.num_samples, data_start_time)
            sampling_task.enter_progress_frame(1,
                                               f'Sampling frame {i + 1:6d} of {param.num_samples:6d} - Time: {passed_time_string} - Left: {est_time_remaining_string}')

    data_elapsed_time = time.time() - data_start_time
    ue.log(f'Calculating inputs and outputs took {data_elapsed_time:.0f} seconds.')

    deltas._mmap.close()

    print('write to', param.inputs_path)
    np.save(param.inputs_path, inputs)


# ====================================== pca ======================================
def save_section_pca(training_dir, section_id, vertex_mean, pca_basis):
    out_path = f'{training_dir}/vertex_mean_{section_id}.npy'
    print('write to', out_path)
    np.save(out_path, vertex_mean)

    out_path = f'{training_dir}/pca_basis_{section_id}.npy'
    print('write to', out_path)
    np.save(out_path, pca_basis)


def load_pca_to_model(param):
    model = param.model
    for section_id in range(param.num_sections):
        ue_section = model.get_section_ptr(section_id)
        vertex_mean = np.load(f'{param.training_dir}/vertex_mean_{section_id}.npy')
        vertex_mean = vertex_mean.astype(np.float32).tolist()
        pca_basis = np.load(f'{param.training_dir}/pca_basis_{section_id}.npy')
        pca_basis = pca_basis.astype(np.float32).reshape(-1).tolist()
        ue_section.set_basis_data(vertex_mean, pca_basis)


def compute_and_save_pca(param):
    global seed
    model = param.model
    num_sections = param.num_sections
    deltas = np.memmap(param.deltas_path, dtype=param.delta_dtype, mode='r', shape=(param.num_samples, param.num_sum_deltas * 3))

    section_to_coeffs = [None for section_id in range(num_sections)]

    with ue.ScopedSlowTask(num_sections, 'Computing PCA Basis') as pca_task:
        pca_task.make_dialog(True)
        for section_id in range(num_sections):
            if pca_task.should_cancel():
                raise GeneratorExit('CannotUse')

            section_deltas = deltas[:, param.get_section_ptr_range(section_id)]
            section_deltas = section_deltas.copy().reshape((param.num_samples, -1))
            pca = PCA(n_components=param.sections[section_id].num_basis, random_state=seed)
            coeffs = pca.fit_transform(section_deltas)
            section_to_coeffs[section_id] = np.ascontiguousarray(coeffs).astype(np.float32)
            vertex_mean = pca.mean_.astype(np.float32)
            pca_basis = np.ascontiguousarray(pca.components_.astype(np.float32))

            save_section_pca(param.training_dir, section_id, vertex_mean, pca_basis)
            pca_task.enter_progress_frame(1, f'Computing PCA Basis {section_id + 1:6d} of {num_sections:6d}')

    deltas._mmap.close()
    outputs = np.concatenate(section_to_coeffs, axis=1)
    print('write to', param.outputs_path)
    np.save(param.outputs_path, outputs)


# ====================================== train ======================================
def load_train_data(param):
    inputs = np.load(param.inputs_path)
    if param.outputs_path.endswith('.npy'):
        outputs = np.load(param.outputs_path)
    elif param.outputs_path.endswith('deltas.bin'):
        outputs = np.memmap(param.outputs_path, dtype=param.delta_dtype, mode='r', shape=(param.num_samples, param.num_sum_deltas * 3))
    else:
        raise RuntimeError('Unknown outputs file type')

    return inputs, outputs


def compute_stats(train_data, device):
    inputs, outputs = train_data
    inputs_mean_np = np.mean(inputs, axis=0)
    inputs_mean = torch.from_numpy(inputs_mean_np).float().to(device)
    inputs_std_np = np.std(inputs, axis=0)
    inputs_std = torch.from_numpy(inputs_std_np).float().to(device)

    outputs_mean_np = np.mean(outputs, axis=0)
    outputs_mean = torch.from_numpy(outputs_mean_np).float().to(device)
    outputs_std_np = np.std(outputs, axis=0)
    outputs_std = torch.from_numpy(outputs_std_np).float().to(device)

    return inputs_mean, inputs_std, outputs_mean, outputs_std


def store_inputs_range_to_model(model, train_data):
    inputs = train_data[0]
    inputs_min = np.min(inputs, axis=0)
    inputs_max = np.max(inputs, axis=0)
    model.inputs_min = inputs_min.tolist()
    model.inputs_max = inputs_max.tolist()


class TensorDataset:
    def __init__(self, inputs, outputs, device=None):
        self.inputs = inputs
        self.outputs = outputs
        self.device = device
        self.is_inputs_torch = isinstance(inputs, torch.Tensor)
        self.is_outputs_torch = isinstance(outputs, torch.Tensor)

    def __getitem__(self, index):
        inputs = self.inputs[index] if self.is_inputs_torch else torch.tensor(self.inputs[index], dtype=torch.float32)
        outputs = self.outputs[index] if self.is_outputs_torch else torch.tensor(self.outputs[index], dtype=torch.float32)
        if self.device is not None:
            inputs = inputs.to(self.device)
            outputs = outputs.to(self.device)
        return inputs, outputs

    def __len__(self):
        return len(self.inputs)


def get_tensor_memory(arr):
    return arr.size * arr.dtype.itemsize


def get_available_gpu_memory():
    if not torch.cuda.is_available():
        return 0
    device_index = torch.cuda.current_device()
    if device_index == -1:
        return 0
    device_free_mem, total_mem = torch.cuda.mem_get_info(device_index)
    allocator_available_mem = torch.cuda.memory_reserved(device_index) - torch.cuda.memory_allocated(device_index)
    print(f'CUDA device_free_mem: { device_free_mem/ (1024 * 1024 * 1024):.2f} GB, CUDA allocator_available_mem: {allocator_available_mem/(1024 * 1024 * 1024):.2f} GB')
    return device_free_mem + allocator_available_mem


def can_store_data_on_device(train_data, param):
    if param.device == 'cpu':
        return True
    inputs, outputs = train_data
    inputs_memory = get_tensor_memory(inputs)
    outputs_memory = get_tensor_memory(outputs)
    total_memory = inputs_memory + outputs_memory
    available_memory = get_available_gpu_memory()
    buffer = 100 * 1024 * 1024
    result = total_memory + buffer <= available_memory
    if not result:
        ue.log_warning(f'Training data requires {(total_memory + buffer) / (1024 * 1024 * 1024):.2f} GB but only {available_memory / (1024 * 1024 * 1024):.2f} GB free.')
    return result


def create_data_loaders(param, train_data):
    inputs, outputs = train_data
    device = param.device
    batch_size = param.batch_size
    num_samples = param.num_samples

    param.store_data_on_device = can_store_data_on_device(train_data, param)

    if param.store_data_on_device:
        try:
            inputs_device = torch.tensor(inputs, dtype=torch.float32, device=device)
            outputs_device = torch.tensor(outputs, dtype=torch.float32, device=device)
            dataset = TensorDataset(inputs_device, outputs_device, device=None)
        except Exception as e:
            ue.log_error('Failed to store data on device. Switching to CPU')
            param.store_data_on_device = False

    if not param.store_data_on_device:
        dataset = TensorDataset(inputs, outputs, device=device)
        ue.log_warning('Switching to storing data on CPU')

    if param.use_val:
        test_dataset_percentage = 0.1
        train_size = int((1.0 - test_dataset_percentage) * len(dataset))
        val_size = len(dataset) - train_size
        train_dataset, val_dataset = torch.utils.data.random_split(dataset, [train_size, val_size])
        train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True)
        val_loader = DataLoader(val_dataset, batch_size=batch_size, shuffle=True)
    else:
        train_dataset = dataset
        train_loader = DataLoader(train_dataset, batch_size=batch_size, shuffle=True)
        val_loader = None

    return train_loader, val_loader


def create_network(param, data_stats):
    inputs_mean, inputs_std, outputs_mean, outputs_std = data_stats

    dims = get_dims(param)

    if param.use_pca:
        network = MLPPCA(dims, inputs_mean, inputs_std, outputs_mean, outputs_std)
    else:
        network = Corrective(dims, inputs_mean, inputs_std, outputs_mean, outputs_std)
    network.to(param.device)
    return network


def train_and_save_network(param, train_data):
    device = param.device
    num_iters = param.num_iters

    data_stats = compute_stats(train_data, device)
    
    network = create_network(param, data_stats)
    train_loader, val_loader = create_data_loaders(param, train_data)
    optimizer = torch.optim.AdamW(network.parameters(), lr=param.lr)
    if param.use_scheduler:
        scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=num_iters)

    if tensorboard_exist:
        log_dir = f'{param.training_dir}/logs/{datetime.datetime.now().strftime("%Y%m%d-%H%M%S")}'
        if not os.path.exists(log_dir):
            os.makedirs(log_dir)
        writer = SummaryWriter(log_dir)

    min_train_loss = float('Inf')
    min_val_loss = float('Inf')
    min_val_epoch = -1

    best_state_dict = None
    succeed = False
    it = 0
    prev_it = 0
    epoch = 0

    training_start_time = time.time()
    try:
        with ue.ScopedSlowTask(num_iters, "Training Model") as training_task:
            training_task.make_dialog(True)

            while True:
                train_loss = 0.0
                network.train()
                for X, Y in train_loader:
                    if training_task.should_cancel():
                        raise GeneratorExit('Usable')
                    optimizer.zero_grad()
                    batch_loss = param.loss_fn(network(X), Y)
                    if param.regularization_factor > 0.0 and hasattr(network, 'morph_target_matrix'):
                        batch_loss += torch.mean(torch.abs(network.morph_target_matrix)) * param.regularization_factor
                    batch_loss.backward()
                    optimizer.step()
                    if param.use_scheduler:
                        scheduler.step()
                    train_loss += batch_loss.item()

                    it += 1
                    if it >= num_iters:
                        break

                train_loss /= len(train_loader.dataset)
                if tensorboard_exist:
                    writer.add_scalar('Loss/train', train_loss, epoch)

                if param.use_val:
                    val_loss = 0.0
                    network.eval()
                    with torch.no_grad():
                        for X, Y in val_loader:
                            if training_task.should_cancel():
                                raise GeneratorExit('Usable')
                            batch_loss = param.loss_fn(network(X), Y)
                            val_loss += batch_loss.item()

                    val_loss /= len(val_loader.dataset)
                    if tensorboard_exist:
                        writer.add_scalar('Loss/val', val_loss, epoch)

                    if val_loss < min_val_loss:
                        min_val_loss = val_loss
                        min_val_epoch = epoch
                        best_state_dict = copy.deepcopy(network.state_dict())

                    if epoch - min_val_epoch > param.early_stop_epochs:
                        break

                    passed_time_string, est_time_remaining_string = generate_time_strings(it, num_iters, training_start_time)
                    training_task.enter_progress_frame(
                        it - prev_it,
                        f'Training iteration: {it + 1:6d} of {num_iters:6d}, Avg val loss: {val_loss:.5f} Time: {passed_time_string} - Left: {est_time_remaining_string}')
                else:  # not use_val
                    if train_loss < min_train_loss:
                        min_train_loss = train_loss
                        best_state_dict = copy.deepcopy(network.state_dict())

                    passed_time_string, est_time_remaining_string = generate_time_strings(it, num_iters, training_start_time)
                    training_task.enter_progress_frame(
                        it - prev_it,
                        f'Training iteration: {it + 1:6d} of {num_iters:6d}, Avg train loss: {train_loss:.5f} Time: {passed_time_string} - Left: {est_time_remaining_string}')

                epoch += 1
                prev_it = it
                if it >= num_iters:
                    break
            succeed = True
            ue.log("Model successfully trained.")
    finally:
        if best_state_dict is not None:
            save_path = f'{param.training_dir}/NearestNeighborModel.pt'
            print('export to', save_path)
            torch.save(best_state_dict, save_path)


def load_morph_targets_and_modify_network(param, network):
    morph_target_matrix = network.morph_target_matrix
    inputs_mean = network.inputs_mean
    inputs_std = network.inputs_std
    delta_mean = network.outputs_mean[0]
    delta_std = network.outputs_std[0]
    network = network.model

    morph_target_matrix = morph_target_matrix * delta_std[:, None]
    sizes = torch.norm(morph_target_matrix, dim=0)
    morph_target_matrix = morph_target_matrix / sizes[None, :]

    n_morph_targets = len(sizes)
    network.inputs_mean = inputs_mean
    network.inputs_std = inputs_std
    network.outputs_mean = nn.Parameter(torch.zeros(1, n_morph_targets))
    network.outputs_std = nn.Parameter(sizes[None, :])

    morph_target_matrix = morph_target_matrix.transpose(1, 0).detach().cpu().numpy().astype(np.float32)
    delta_mean = delta_mean.detach().cpu().numpy().astype(np.float32)

    model = param.model
    for section_id in range(param.num_sections):
        ue_section = model.get_section_ptr(section_id)
        section_range = param.get_section_ptr_range(section_id)
        vertex_mean = delta_mean[section_range]
        vertex_mean = vertex_mean.astype(np.float32).tolist()
        basis = morph_target_matrix[:, section_range]
        if basis.shape != (n_morph_targets, ue_section.num_vertices * 3):
            raise RuntimeError(f'Morph target matrix shape mismatch, expected {n_morph_targets} x {ue_section.num_vertices * 3}, got {basis.shape}')
        basis = basis.astype(np.float32).reshape(-1).tolist()
        ue_section.set_basis_data(vertex_mean, basis)

    return network


def save_ubnne_network(param):
    training_dir = param.training_dir

    state_dict = torch.load(f'{training_dir}/NearestNeighborModel.pt')
    network = create_empty_network(param)
    network.load_state_dict(state_dict)
    network.eval()

    if not param.use_pca:
        network = load_morph_targets_and_modify_network(param, network)

    with torch.no_grad():

        layer_list = []

        layer_list.append({
            'Type': 'Normalize',
            'Mean': network.inputs_mean[0].cpu().numpy().astype(np.float32),
            'Std': network.inputs_std[0].cpu().numpy().astype(np.float32),
        })

        for li, layer in enumerate(network.model):

            if isinstance(layer, nn.Linear):

                layer_list.append({
                    'Type': 'Linear',
                    'Weights': layer.weight.T.cpu().numpy().astype(np.float32),
                    'Biases': layer.bias.cpu().numpy().astype(np.float32),
                })

            elif isinstance(layer, nn.PReLU):

                # This network has a single alpha value but our format expects
                # one alpha per neuron so we need to get the size of bias on previous 
                # linear layer and repeat the single alpha value we have. 
                size = len(network.model[li - 2].bias)

                layer_list.append({
                    'Type': 'PReLU',
                    'Alpha': layer.weight.cpu().numpy().repeat(size).astype(np.float32),
                })

            elif isinstance(layer, nn.BatchNorm1d):

                layer_list.append({
                    'Type': 'Normalize',
                    'Mean': layer.running_mean.cpu().numpy().astype(np.float32),
                    'Std': np.sqrt(layer.running_var.cpu().numpy() + layer.eps).astype(np.float32),
                })

                layer_list.append({
                    'Type': 'Denormalize',
                    'Mean': layer.bias.cpu().numpy().astype(np.float32),
                    'Std': layer.weight.cpu().numpy().astype(np.float32),
                })
            elif isinstance(layer, nn.ELU):
                size = torch.prod(torch.tensor(network.model[li - 1].bias.shape)).item()
                layer_list.append({
                    'Type': 'ELU',
                    'Size': size,
                })
            else:
                raise Exception('Unexpected Layer type!')

        layer_list.append({
            'Type': 'Denormalize',
            'Mean': network.outputs_mean[0].cpu().numpy().astype(np.float32),
            'Std': network.outputs_std[0].cpu().numpy().astype(np.float32),
        })

    _, layers = nne_runtime_basic_cpu.optimize({'Type': 'Sequence', 'Layers': layer_list})

    out_path = f'{training_dir}/NearestNeighborModel.ubnne'

    with open(out_path, 'wb') as f:

        nne_runtime_basic_cpu.serialization_save_model_to_file(f, layers)
        print('network exported to', out_path)


def remove_files_in_dir(d, skip_files):
    if os.path.exists(d):
        for f in os.listdir(d):
            if not f in skip_files:
                file_path = os.path.join(d, f)
                if os.path.isfile(file_path):
                    os.remove(file_path)


def cleanup(training_dir):
    remove_files_in_dir(training_dir, skip_files=['NearestNeighborModel.ubnne'])


def train(api):
    return_code = 0  # success
    try:
        # Training Parameters
        rand_seed()
        param = TrainParam(api)
        param.init_train_param()

        if param.use_debug_mode:
            save_tpose_data(param)

        if param.recompute_deltas:
            compute_and_save_deltas(param)

        if param.use_pca:
            if param.recompute_pca:
                compute_and_save_pca(param)
            load_pca_to_model(param)

        if param.skip_training:
            raise param.skip_training_except

        if param.train_network:
            train_data = load_train_data(param)
            store_inputs_range_to_model(param.model, train_data)
            train_and_save_network(param, train_data)

    except GeneratorExit as e:
        ue.log_warning("Training canceled by user.")
        if str(e) == 'Usable':
            return_code = 1  # 'aborted useable'
        else:
            return_code = 2  # 'aborted not useable'

    except Exception as e:
        logging.error(e)
        logging.error(traceback.format_exc())
        return_code = 2  # 'error'

    finally:
        if 'train_data' in locals():
            inputs, outputs = train_data
            if isinstance(outputs, np.memmap):
                outputs._mmap.close()
            del inputs, outputs
            
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    if return_code == 0 or return_code == 1:
        try:
            save_ubnne_network(param)
        except Exception as e:
            logging.error(e)
            logging.error(traceback.format_exc())
            return_code = 4  # 'python error'

    if return_code == 0 and not param.use_file_cache:
        cleanup(param.training_dir)

    return return_code
