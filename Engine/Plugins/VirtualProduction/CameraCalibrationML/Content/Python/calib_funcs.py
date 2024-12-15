# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

import numpy as np
import cv2
import math
from typing import Tuple

import torch
from torch import Tensor
from torch.utils.data import DataLoader

from cam_group import CamGroup
from distortion import LensDistortion
from dataset import CornersDataset

from calibration_utils import log_timing_metrics, start_timing, stop_timing

def make_intrinsics_matrix(fx: float, fy: float, cx: float, cy: float) -> np.ndarray:
    return np.array([[fx, 0, cx], [0, fy, cy], [0, 0, 1.0]])

def get_range(point_coords: np.ndarray):
    return tuple(
        np.concatenate(
            (
                point_coords.min(axis=0).min(axis=0, keepdims=True),
                point_coords.max(axis=0).max(axis=0, keepdims=True),
            ),
            axis=0,
        ).T.flatten()
    )

def make_rotation_matrices(
    r_vecs: np.ndarray,  # (num_images, 3)
) -> np.ndarray:
    assert len(r_vecs.shape) == 2
    assert r_vecs.shape[1] == 3
    num_images = r_vecs.shape[0]
    r_mats = []
    for i in range(num_images):
        r_mats.append(cv2.Rodrigues(r_vecs[i, :])[0])

    return np.array(r_mats)

def render_stmap(distortion_map, undistortion_map):
    image_width = distortion_map.shape[0]
    image_height = distortion_map.shape[1]

    img = np.zeros((image_height, image_width, 4), dtype=np.float32) # (H, W, 4)

    B, G, R, A = (0, 1, 2, 3) # Indices based on the formatting of color channels
    img[:, :, [R,G]] = np.swapaxes(undistortion_map, 0, 1)
    img[:, :, [B,A]] = np.swapaxes(distortion_map, 0, 1)

    return img

def compute_displacement_map(
    distortion: LensDistortion,
    intrinsics: Tuple[float, float, float, float],
    resolution_wh: Tuple[int, int],
    resolution_stmap: Tuple[int, int],
) -> Tuple[Tensor, Tensor]:

    prev_time = start_timing()
    
    uv_range = CamGroup.sensor_coord_range(
        resolution_wh=resolution_wh,
        intrinsics=intrinsics,
        distortion=None,
    )

    distortion_map = distortion.st_map(
        stmap_resolution=resolution_stmap,
        image_resolution=resolution_wh,
        intrinsics=intrinsics,
        uv_range=uv_range,
        sensor_to_frustum=True,
    )

    undistortion_map = distortion.st_map(
        stmap_resolution=resolution_stmap,
        image_resolution=resolution_wh,
        intrinsics=intrinsics,
        uv_range=uv_range,
        sensor_to_frustum=False,
    )

    prev_time = log_timing_metrics(prev_time, "calib_func.compute_displacement_map", "Compute ST Map")

    stop_timing()

    return distortion_map, undistortion_map

def project_points_cv(
    object_points: np.ndarray,  # (num_frames, n_pts, 3)
    r_vecs: np.ndarray,         # (num_frames, 3)
    t_vecs: np.ndarray,         # (num_frames, 3)
    camera_matrix: np.ndarray,  # (3, 3)
    dist_coeffs: np.ndarray,    # (8, )
) -> np.ndarray:                # (num_frames, n_pts, 2)

    num_frames, num_points, _ = object_points.shape
    points_all_frames = np.zeros((num_frames, num_points, 2))

    for frame_index in range(num_frames):
        projected_points, _ = cv2.projectPoints(
            objectPoints=object_points[frame_index, :, :].astype(np.float32),
            rvec=r_vecs[frame_index],
            tvec=t_vecs[frame_index],
            cameraMatrix=camera_matrix,
            distCoeffs=dist_coeffs,
        )

        points_all_frames[frame_index, :, :] = projected_points.reshape(num_points, 2)

    return points_all_frames

def compute_rmse(observed_pixels: Tensor, projected_pixels: Tensor) -> Tensor:
    observed_pixels = observed_pixels.reshape(-1, 2)
    projected_pixels = projected_pixels.reshape(observed_pixels.shape)

    diff = observed_pixels - projected_pixels
    rmse = torch.sqrt(torch.sum(torch.square(diff) / observed_pixels.shape[0]))

    return rmse

def calibrate_with_opencv(
    solver,
    point_coords,  # (num_images, num_points, 3)
    pixel_coords,  # (num_images, num_points, 2)
    train_frames: list[int],
    val_frames: list[int],
    intrinsics_guess: np.ndarray,  # (3, 3)
    resolution_wh: Tuple[int, int],
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    num_images = point_coords.shape[0]

    # Initialize the array of output rvecs and tvecs that will be computed
    r_vecs = np.zeros((num_images, 3), dtype=np.float32)
    t_vecs = np.zeros((num_images, 3), dtype=np.float32)

    # Train opencv model on train frames
    (train_rmse, camera_matrix, dist_coeffs, r_vecs_train, t_vecs_train) = cv2.calibrateCamera(
        objectPoints=point_coords[train_frames, :, :].astype(np.float32),
        imagePoints=pixel_coords[train_frames, :, :].astype(np.float32),
        imageSize=resolution_wh,
        cameraMatrix=intrinsics_guess,
        distCoeffs=None,
        flags=cv2.CALIB_USE_INTRINSIC_GUESS,
    )

    # Write extrinsics for training frames
    r_vecs[train_frames, :] = np.array(r_vecs_train).reshape(-1, 3)
    t_vecs[train_frames, :] = np.array(t_vecs_train).reshape(-1, 3)

    # Compute extrinsics for validation frames using opencv model
    r_vecs_val = []
    t_vecs_val = []

    for frame in val_frames:
        (retval_val, r_vec_val, t_vec_val) = cv2.solvePnP(
            objectPoints=point_coords[frame, :, :].astype(np.float32),
            imagePoints=pixel_coords[frame, :, :].astype(np.float32),
            cameraMatrix=camera_matrix,
            distCoeffs=dist_coeffs,
        )
        r_vecs_val.append(r_vec_val)
        t_vecs_val.append(t_vec_val)

    # Evaluate rmse for validation frames
    projected_points_val = project_points_cv(
        object_points=point_coords[val_frames, :, :],
        r_vecs=r_vecs_val,
        t_vecs=t_vecs_val,
        camera_matrix=camera_matrix,
        dist_coeffs=dist_coeffs,
    )

    val_rmse = compute_rmse(
        observed_pixels=torch.from_numpy(pixel_coords[val_frames, :, :]),
        projected_pixels=torch.from_numpy(projected_points_val)
    )

    # Write extrinsics for validation frames
    r_vecs[val_frames, :] = np.array(r_vecs_val).reshape(-1, 3)
    t_vecs[val_frames, :] = np.array(t_vecs_val).reshape(-1, 3)

    stop_timing()

    fx = camera_matrix[0][0]
    fy = camera_matrix[1][1]
    cx = camera_matrix[0][2]
    cy = camera_matrix[1][2]

    k1, k2, p1, p2, k3 = dist_coeffs.flatten()

    print(f"opencv focal length=({fx:.9f}, {fy:.9f})")
    print(f"opencv image center=({cx:.9f}, {cy:.9f})")
    print(f"opencv distortion=({k1:.9f}, {k2:.9f}, {p1:.9f}, {p2:.9f}, {k3:.9f})")
    print(f"opencv train rmse={float(train_rmse):.9f}, val rmse={float(val_rmse):.9f}")

    return (dist_coeffs, r_vecs, t_vecs)

def calibrate_with_neural_net(
    solver,
    point_coords: Tensor,  # (num_images, num_points, 3)
    pixel_coords: Tensor,  # (num_images, num_points, 2)
    train_frames: list[int],
    val_frames: list[int],
    cam_group: CamGroup,
    resolution_wh: Tuple[int, int],
) -> Tuple[np.ndarray, LensDistortion, np.ndarray, np.ndarray, float, float]:
    num_images, num_points, _ = point_coords.shape

    # Ensure that the input object points are actually 3D
    assert point_coords.shape[2] == 3

    # Ensure that the input image points are actually 2D
    assert pixel_coords.shape[2] == 2

    # Ensure that the number of 2D points matches the number of 3D points
    assert pixel_coords.shape[0] == num_images
    assert pixel_coords.shape[1] == num_points

    # Setup datasets
    train_dataset = CornersDataset(
        pixels=pixel_coords[train_frames, :, :],
        frame_inds=torch.tensor(train_frames),
        resolution_wh=resolution_wh,
    )

    val_dataset = CornersDataset(
        pixels=pixel_coords[val_frames, :, :],
        frame_inds=torch.tensor(val_frames),
        resolution_wh=resolution_wh,
    )

    # Setup a DataLoader for each dataset with specified batch size 
    batch_size=256
    train_dataloader = DataLoader(train_dataset, batch_size=batch_size)
    val_dataloader = DataLoader(val_dataset, batch_size=batch_size)

    # Setup optimization
    num_epochs = 1
    num_validation_epochs = 1 # TODO: In practice, this appears to be "number of validation epochs per training epoch"

    lr_cam_params = 1e-5
    lr_distortion = 1e-6

    lr_schedule_pct_start = 0.3 # This is the default for the 1cycle optimizer

    # Create an optimizer for the training set using the Adam algorithm for the camera parameters (intrinsics and extrinsics) and the distortion network
    optimizer_train = torch.optim.Adam(
        [
            {"params": cam_group.cam_params, "lr": lr_cam_params},
            {"params": cam_group.distortion.parameters(), "lr": lr_distortion},
        ]
    )

    # Create a learning rate scheduler to adjust the learning rate for the training optimizer
    # The 1cycle learning rate policy changes the learning rate after every batch
    num_batches_per_epoch_train = math.ceil(len(train_dataset) / batch_size)

    scheduler_train = torch.optim.lr_scheduler.OneCycleLR(
        optimizer_train,
        max_lr=[lr_cam_params, lr_distortion],
        total_steps=num_epochs * num_batches_per_epoch_train,
        pct_start=lr_schedule_pct_start,
    )

    # Create an optimizer for the validation set using the Adam algorithm for just the camera parameters (intrinsics and extrinsics)
    optimizer_val = torch.optim.Adam(
        [
            {"params": cam_group.cam_params, "lr": lr_cam_params},
        ]
    )

    # Create a learning rate scheduler to adjust the learning rate for the validation optimizer
    num_batches_per_epoch_val = math.ceil(len(val_dataset) / batch_size)

    scheduler_val = torch.optim.lr_scheduler.OneCycleLR(
        optimizer_val,
        max_lr=[lr_cam_params],
        total_steps=num_validation_epochs * num_batches_per_epoch_val,
        pct_start=lr_schedule_pct_start,
    )

    # Mask so that only extrinsic parameters are updated during pose optimization on validation frames
    val_extrinsics_mask = torch.zeros_like(cam_group.cam_params.data) # (num_images, num_cam_params(10))
    for i in val_frames:
        val_extrinsics_mask[i, cam_group.num_intrinsics :] = 1.0

    # Optimize
    step_count = 0
    epoch_start_time = start_timing()
    for epoch in range(num_epochs):
        # Training
        for observed_pixels, frame_inds, pt_inds in train_dataloader: # (batch_size, 2), (batch_size), (batch_size)
            # Reset the gradients of all optimized tensors in the training optimizer
            optimizer_train.zero_grad()

            # Project this batch of 3d object points to 2d
            projected_pixels = cam_group.project_points(
                points_3d=point_coords[frame_inds, pt_inds, :],
                frame_inds=frame_inds,
            )

            # Compute error (diff) between observed pixels and projected pixels
            loss_reprojection = torch.nn.functional.smooth_l1_loss(observed_pixels, projected_pixels)

            # Compute the weighted variance for the intrinsic parameters across images
            fxfy_weight=1e-1
            cxcy_weight=1e-2
            loss_intrinsic_variance = cam_group.intrinsic_variance(fxfy_weight=fxfy_weight, cxcy_weight=cxcy_weight)

            loss = loss_reprojection + loss_intrinsic_variance

            # Compute the gradient 
            loss.backward()

            # Try to mitigate against exploding gradients (prevent divergence)
            grad_max_norm=0.5
            torch.nn.utils.clip_grad_norm_(cam_group.distortion.parameters(), max_norm=grad_max_norm)

            # Update the camera parameters and distortion network
            optimizer_train.step()

            # Update the learning rate
            scheduler_train.step()

            if (step_count + 1) % 50 == 0 or (step_count + 1) == num_epochs * len(train_dataset):
                print(f"step={step_count}, loss={loss_reprojection.item():0.4e}")

            step_count = step_count + 1

        epoch_start_time = log_timing_metrics(epoch_start_time, "calibrate_with_neural_net", "Epoch #" + str(epoch))

        # Validation
        val_every_n_epoch=5
        if epoch == 0 or (epoch + 1) % val_every_n_epoch == 0:
            # Update camera intrinsics for validation frames with the average intrinsic values from the training frames
            with torch.no_grad():
                avg_intrinsic_params = cam_group.cam_params[train_frames, 0 : cam_group.num_intrinsics].mean(dim=0, keepdim=True)
                cam_group.cam_params[val_frames, 0 : cam_group.num_intrinsics] = avg_intrinsic_params

            val_step_count = 0
            num_validation_epochs = 0  # TODO: Ask Yi about why this is currently set to 0
            for val_epoch in range(num_validation_epochs):
                for observed_pixels, frame_inds, pt_inds in val_dataloader: # (batch_size, 2), (batch_size), (batch_size)
                    # Reset the gradients of all optimized tensors in the training optimizer
                    optimizer_val.zero_grad()

                    # Project this batch of 3d object points to 2d
                    projected_pixels = cam_group.project_points(
                        points_3d=point_coords[frame_inds, pt_inds, :],
                        frame_inds=frame_inds,
                    )

                    # Compute error (diff) between observed pixels and projected pixels
                    loss_reprojection = torch.nn.functional.smooth_l1_loss(observed_pixels, projected_pixels)

                    # Compute the gradient 
                    loss_reprojection.backward()
                    
                    # Mask out all of the parameter gradients except the validation extrinsics
                    cam_group.cam_params.grad = (cam_group.cam_params.grad * val_extrinsics_mask)

                    # Update the camera parameters and distortion network
                    optimizer_val.step()

                    # Update the learning rate
                    scheduler_val.step()

                    if (val_step_count + 1) % 50 == 0 or (val_step_count + 1) == num_validation_epochs * len(val_dataset):
                        print(f"val step={val_step_count}, loss={loss_reprojection.item():0.4e}")

                    val_step_count = val_step_count + 1

            with torch.no_grad():
                # eval val rmse
                projected_pixels = cam_group.project_points(
                    points_3d=point_coords[val_frames, :, :].reshape(-1, 3),
                    frame_inds=torch.tensor(val_frames)
                    .view(-1, 1)
                    .expand(-1, num_points)
                    .flatten(),
                )
                val_rmse = torch.nn.functional.mse_loss(pixel_coords[val_frames, :, :].view(-1, 2), projected_pixels).sqrt()
                print(f"{epoch=} {val_rmse=}")

    # Compute the final training and validation rmse values
    with torch.no_grad():
        # Compute train rmse
        projected_pixels = cam_group.project_points(
            points_3d=point_coords[train_frames, :, :].reshape(-1, 3),
            frame_inds=torch.tensor(train_frames).view(-1, 1).expand(-1, num_points).flatten(),
        )

        train_rmse = torch.nn.functional.mse_loss(pixel_coords[train_frames, :, :].view(-1, 2), projected_pixels).sqrt()
        print(f"final {train_rmse=}")

        # Compute val rmse
        projected_pixels = cam_group.project_points(
            points_3d=point_coords[val_frames, :, :].reshape(-1, 3),
            frame_inds=torch.tensor(val_frames).view(-1, 1).expand(-1, num_points).flatten(),
        )

        val_rmse = torch.nn.functional.mse_loss(pixel_coords[val_frames, :, :].view(-1, 2), projected_pixels).sqrt()
        print(f"final {val_rmse=}")

    # Format outputs
    with torch.no_grad():
        fx, fy, cx, cy = cam_group.avg_intrinsics()
        intrinsics = (float(fx), float(fy), float(cx), float(cy))

        distortion = cam_group.distortion

        r_mats = cam_group.rotations
        r_vecs = []
        for i in range(r_mats.shape[0]):
            r_vecs.append(cv2.Rodrigues(r_mats[i, :, :].detach().cpu().numpy())[0])
        r_vecs = np.array(r_vecs)

        t_vecs = cam_group.translations
        t_vecs = t_vecs.detach().cpu().numpy()

    stop_timing()

    return (intrinsics, distortion, r_vecs, t_vecs, float(train_rmse), float(val_rmse))
