# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

# For debugging, it is convenient to run this script outside of Unreal, in which case, the Unreal library may not exist
try:
    import unreal
except ImportError:
    pass

import os
os.environ["OPENCV_IO_ENABLE_OPENEXR"]="1"

import numpy as np
import cv2
import torch
import json
from datetime import datetime
import gc
import sys

from torch.utils.data import random_split

from distortion import OpenCV8ParamDistortion
from cam_group import CamGroup

from calib_funcs import (
    calibrate_with_opencv,
    calibrate_with_neural_net,
    make_intrinsics_matrix,
    make_rotation_matrices,
    compute_displacement_map,
    render_stmap,
    get_range
)

from calibration_utils import ue_to_np, log_timing_metrics

# Disables scientific notation when printing numpy arrays
np.set_printoptions(suppress=True)

# Set to True to make this solver appear as an option in the Lens Distortion Tool UI
# Set to False to hide this solver in the UI
def is_enabled():
    return True

def solve(self, object_points_array, image_points_array, image_size, focal_length, image_center, init_distortion, camera_poses):
    print("Running pytorch calibration solver...")

    samples3d = []
    samples2d = []

    for object_points in object_points_array:
        samples3d.append(ue_to_np(object_points.points)) 

    for image_points in image_points_array:
        samples2d.append(ue_to_np(image_points.points))

    image_size_tuple = (image_size.x, image_size.y)
    focal_length_tuple = (focal_length.x, focal_length.y)
    image_center_tuple = (image_center.x, image_center.y)

    start_time = datetime.now()

    (rmse_torch, focal_length_torch, image_center_torch, stmap) = solve_torch(
        self, 
        samples3d, 
        samples2d, 
        image_size_tuple, 
        focal_length_tuple, 
        image_center_tuple
    )

    end_time = datetime.now()
    diff = end_time - start_time
    print("Total Time: " + str(diff.total_seconds()) + " seconds")

    timestamp_string = end_time.strftime("%m-%d-%Y_%H-%M-%S")
    stmap_filename = unreal.Paths.project_content_dir() + "STMap_" + timestamp_string + ".exr"
    cv2.imwrite(stmap_filename, stmap)

    torch.cuda.empty_cache()
    gc.collect()

    result = unreal.DistortionCalibrationResult()
    result.reprojection_error = rmse_torch
    result.st_map_full_path = stmap_filename

    focal_length_result = unreal.FocalLengthInfo()
    focal_length_result.fx_fy = unreal.Vector2D(focal_length_torch[0], focal_length_torch[1])
    result.focal_length = focal_length_result

    image_center_result = unreal.ImageCenterInfo()
    image_center_result.principal_point = unreal.Vector2D(image_center_torch[0], image_center_torch[1])
    result.image_center = image_center_result

    return result

def solve_torch(self, samples3d, samples2d, image_size, focal_length, image_center):
    prev_time = datetime.now()

    # Set to "cuda" to run on gpu or "cpu" to run on cpu
    device_name = "cuda" 
    device = torch.device(device_name)

    with torch.device(device):
        # Set up a generator for the dataset split
        manual_seed = 42
        generator = torch.Generator(device=device).manual_seed(manual_seed)

        # Ratio of images that will go into the training and validation datasets, respectively
        dataset_ratio = [0.85, 0.15]

        # The current version of pytorch used by UE does not support fractional ratios, so we must compute the absolute lengths for the split
        n_frames = len(samples3d)
        dataset_lengths = [int(ratio * n_frames) for ratio in dataset_ratio]
        dataset_lengths[0] = n_frames - sum(dataset_lengths[1:])

        # Split the image indices between the training and validation datasets
        dataset_index_split = random_split(range(n_frames), dataset_lengths, generator=generator)

        train_frames = list(dataset_index_split[0].indices)
        val_frames = list(dataset_index_split[1].indices)

        # Calibrate with OpenCV
        intrinsics_guess = make_intrinsics_matrix(fx=focal_length[0], fy=focal_length[1], cx=image_center[0], cy=image_center[1])

        (dist_coeffs_cv, r_vecs_cv, t_vecs_cv) = calibrate_with_opencv(
            solver=self,
            point_coords=np.array(samples3d),
            pixel_coords=np.array(samples2d),
            train_frames=train_frames,
            val_frames=val_frames,
            intrinsics_guess=intrinsics_guess,
            resolution_wh=image_size,
        )

        # Initialize OpenCV distortion network
        distortion_cv = OpenCV8ParamDistortion(params=dist_coeffs_cv)
        
        # Calculate min/max range of the scene volume: (xmin xmax ymin ymax zmin zmax)
        volume_range = get_range(np.array(samples3d))

        # Convert OpenCV rvecs from Rodrigues format into rotation matrices
        r_mats_cv = make_rotation_matrices(r_vecs_cv)
        
        # Setup cam_group
        cam_group = CamGroup(
            solver=self,
            distortion_init=distortion_cv,
            intrinsics_guess=intrinsics_guess, # TODO: Should this be the initial user guess or the calibrated intrinsics from opencv?
            r_mats=torch.from_numpy(r_mats_cv).to(device=device), # from_numpy() does not use the default device, so must specify explicitly
            t_vecs=torch.from_numpy(t_vecs_cv).to(device=device), # from_numpy() does not use the default device, so must specify explicitly
            resolution_wh=image_size,
            volume_range=volume_range,
            manual_seed=manual_seed,
        )

        prev_time = log_timing_metrics(prev_time, "solve_torch", "Initialize CamGroup")

        # Neural Lens calibration with intrinsic, distortion, and extrinsics guess
        (
            intrinsics_nl,
            distortion_nl,
            r_mats_nl, # Currently unused, but may be useful later for outputting camera poses
            t_vecs_nl, # Currently unused, but may be useful later for outputting camera poses
            train_rmse_nl,
            val_rmse_nl
        ) = calibrate_with_neural_net(
            solver=self,
            point_coords=torch.tensor(samples3d),
            pixel_coords=torch.tensor(samples2d),
            train_frames=train_frames,
            val_frames=val_frames,
            cam_group=cam_group,
            resolution_wh=image_size,
        )
        
        prev_time = log_timing_metrics(prev_time, "solve_torch", "Solve neural network")
        
        # Calculate STMap for distortion network
        resolution_stmap=(256, 256)

        with torch.no_grad():
            distortion_map, undistortion_map = compute_displacement_map(
                intrinsics=intrinsics_nl,
                distortion=distortion_nl,
                resolution_wh=image_size,
                resolution_stmap=resolution_stmap,
            )

        prev_time = log_timing_metrics(prev_time, "solve_torch", "Calculate STMap for distortion network")

        torch.cuda.empty_cache()

        distortion_map_cpu = distortion_map.cpu().numpy()
        undistortion_map_cpu = undistortion_map.cpu().numpy()

        st_map = render_stmap(distortion_map_cpu, undistortion_map_cpu)

        prev_time = log_timing_metrics(prev_time, "solve_torch", "Render STMap to image")

        focal_length_result = [intrinsics_nl[0], intrinsics_nl[1]]
        image_center_result = [intrinsics_nl[2], intrinsics_nl[3]]

        return (train_rmse_nl, focal_length_result, image_center_result, st_map)

# The following main function is provided to make it possible to run the pytorch calibration from outside of Unreal.
# It relies on having a dataset exported to json, and is primarily used to accelerate iteration times for testing and development.
if __name__ == "__main__":
    print("Running pytorch calibration solver from CLI...")

    # Read json file
    json_filename = sys.argv[1]
    with open(json_filename, "r") as inf:
        data = json.load(inf)

    object_points = data["board_coordinates_xyz"]
    image_points = data["frame_coordinates_xy"]
    resolution_wh = data["resolution_wh"]
    focal_length = data["focal_length"]
    image_center = data["image_center"]

    start_time = datetime.now()

    rmse, focal_length_result, image_center_result, test_stmap = solve_torch(
        None, 
        object_points, 
        image_points, 
        resolution_wh, 
        focal_length, 
        image_center
    )

    end_time = datetime.now()
    diff = end_time - start_time
    print("Total Time: " + str(diff.total_seconds()) + " seconds")

    print(rmse)
    print(focal_length_result)
    print(image_center_result)

    cv2.imwrite(f"stmap.exr", test_stmap)
    