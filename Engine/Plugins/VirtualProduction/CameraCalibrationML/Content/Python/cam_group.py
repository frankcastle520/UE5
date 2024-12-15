# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

import numpy as np
from typing import Tuple, Optional

import torch
from torch import Tensor
from torch.autograd.functional import jacobian

import scipy

from distortion import LensDistortion, NeuralLensDistortion

from calibration_utils import log_timing_metrics, start_timing, stop_timing

from barf.rotations import se3_to_SE3, SE3_to_se3

class CamGroup(torch.nn.Module):
    """
    A camera group looking at a coherent scene or object together.

    This data structure has to give fast access to raycasting operations
    for fast pixel iteration and optimization.
    """

    def __init__(
        self,
        solver,
        distortion_init: LensDistortion,
        intrinsics_guess: np.ndarray,  # (3, 3)
        r_mats: torch.Tensor,  # (num_frames, 3, 3), world to camera
        t_vecs: torch.Tensor,  # (num_frames, 3), world to camera
        resolution_wh: Tuple[int, int],
        volume_range: Tuple[float, float, float, float, float, float],
        manual_seed: int = 42,
    ):
        super().__init__()

        prev_time = start_timing()

        self.num_frames = r_mats.shape[0]
        self.resolution_wh = resolution_wh

        # Ensure that r_mats are 3x3 matrices
        assert r_mats.shape[1] == 3 and r_mats.shape[2] == 3

        # Ensure that there are the same number of t_vecs are r_mats
        assert t_vecs.shape[0] == self.num_frames

        # Ensure that the t_vecs are Vector3
        assert t_vecs.shape[1] == 3

        fx = intrinsics_guess[0, 0]
        fy = intrinsics_guess[1, 1]
        cx = intrinsics_guess[0, 2]
        cy = intrinsics_guess[1, 2]

        self.intrinsics_init = torch.tensor([[fx, fy, cx, cy]], dtype=torch.float32)

        # Join r_mats and t_vecs into a 3x4 matrix (per frame)
        Rt = torch.cat((r_mats, t_vecs.view(self.num_frames, 3, 1)), dim=2) # (num_frames, 3, 4)
        self.se3_init = SE3_to_se3(Rt)  

        # cam_params hold intrinsics & extrinsics of frame i in cam_params[i, :] in the order of
        # [fx fy cx cy; r_vec; t_vec]
        self.num_intrinsics = self.intrinsics_init.shape[1]
        self.num_extrinsics = 6
        num_cam_params = self.num_intrinsics + self.num_extrinsics

        self.cam_params = torch.nn.Parameter(torch.zeros((self.num_frames, num_cam_params), dtype=torch.float32))

        # Init Camera Preconditioning matrices for each frame
        self.precondition_mat_invs = torch.eye(num_cam_params, dtype=torch.float32).repeat(self.num_frames, 1, 1)

        # Setup distortion network
        self.distortion = NeuralLensDistortion(
            num_nodes=2,
            internal_size=1024,
            num_internal_layers=2,
            inp_size_linear=2,
        )

        self.init_distortion_network(dist_init=distortion_init, focal_length=np.mean([fx, fy]))

        # Compute Camera Preconditioning matrices
        with torch.no_grad():
            self.precondition_mat_invs = self.compute_conditioning_matrix(
                solver,
                volume_range=volume_range,
                manual_seed=manual_seed,
            )

        prev_time = log_timing_metrics(prev_time, "CamGroup.__init__", "Compute CamP Pre-Conditioning Matrix")
        stop_timing()

    def compute_conditioning_matrix(
        self,
        solver,
        volume_range: Tuple[float, float, float, float, float, float],
        manual_seed: int,
    ) -> Tensor:
        # Generate num_points random points in volume
        x_min, x_max, y_min, y_max, z_min, z_max = volume_range

        scale = torch.tensor([[x_max - x_min, y_max - y_min, z_max - z_min]], dtype=torch.float32)
        offset = torch.tensor([[x_min, y_min, z_min]], dtype=torch.float32)

        sampler = torch.quasirandom.SobolEngine(dimension=3, scramble=True, seed=manual_seed)

        num_points = 64
        points = sampler.draw(num_points).to(scale.device) * scale + offset  # (num_points, 3)

        # Init empty list of preconditioning matrices
        precondition_mat_invs = []

        num_frames = self.se3_init.shape[0]

        loop_start_time = start_timing()
        for i in range(self.num_frames):
            if (solver and solver.is_running() == False):
                return

            def project_with_cam_params(cam_params_ith_frame):
                # Copy the camera parameters, inserting the input params for the ith frame
                cam_params = self.cam_params.data.clone()
                cam_params[i, :] = cam_params_ith_frame

                projected_points = self.project_points(
                    cam_params=cam_params,
                    points_3d=points,
                    frame_inds=torch.full((num_points,), fill_value=i, dtype=torch.int64)
                )

                rescale = 1.0 / max(self.resolution_wh)
                return projected_points.flatten() * rescale

            # Compute Jacobian of projection function
            # In the original CamP paper, the projection equation is represented by pi
            J_pi = jacobian(func=project_with_cam_params, inputs=self.cam_params.data[i, :]) # (num_points * 2, num_cam_params)

            # In the original CamP paper, the covariance matrix is computed as (Jt @ J) and is represented by sigma_pi
            sigma_pi = J_pi.t() @ J_pi

            # Add a small dampening factor to the diagonals of the covariance matrix
            diag_weight = 1e-1
            eye_weight = 1e-8

            sigma_pi_dampened = (
                sigma_pi 
                + diag_weight * sigma_pi.diag().diag() 
                + eye_weight * torch.eye((sigma_pi.shape[0]), dtype=sigma_pi.dtype)
            )

            # Take the inverse square root of the covariance matrix to get the conditioning matrix
            # Convert from torch to numpy, compute sqrtm, then convert back to torch
            sigma_pi_np = sigma_pi_dampened.detach().cpu().numpy()
            sigma_pi_sqrt_np = scipy.linalg.sqrtm(sigma_pi_np)
            sigma_pi_sqrt = torch.from_numpy(sigma_pi_sqrt_np).to(sigma_pi_dampened)

            p_inv = torch.inverse(sigma_pi_sqrt)

            precondition_mat_invs.append(p_inv)

            print(f"precondition mat frame={i}/{self.num_frames}, jac max={J_pi.max():.2f}, cov max={sigma_pi.max():.2f}")

            loop_start_time = log_timing_metrics(loop_start_time, "CamGroup.compute_conditioning_matrix", "Image #" + str(i))

        stop_timing()
        return torch.stack(precondition_mat_invs, dim=0)

    def init_distortion_network(self, dist_init: LensDistortion, focal_length: float):
        # If no distortion network is provided, skip the initialization
        if (dist_init is None):
            return

        fx, fy, cx, cy = self.intrinsics[0, :]
        sensor_range = self.sensor_coord_range(
            resolution_wh=self.resolution_wh,
            intrinsics=self.intrinsics[0, :],
            distortion=self.distortion
        )

        self.distortion.pretrain(
            distort_gt=lambda pts_frustum: dist_init.forward(pts_frustum, sensor_to_frustum=False),
            input_range=sensor_range,
            focal_length=focal_length
        )

    @staticmethod
    def sensor_coord_range(
        resolution_wh,
        intrinsics : Tuple[float, float, float, float],
        distortion: Optional[LensDistortion] = None,
    ) -> Tuple[float, float, float, float]:
        width, height = resolution_wh
        fx, fy, cx, cy = intrinsics

        # Compute the min/max values in x/y in normalized camera coordinate space
        us = torch.tensor([0, width - 1, 0, width - 1], dtype=torch.int32)
        vs = torch.tensor([0, 0, height - 1, height - 1], dtype=torch.int32)

        xys = torch.stack(
            (
                (us - cx) / fx, 
                (vs - cy) / fy, 
            ), dim=-1)

        # Undistort
        if distortion is not None:
            xys = distortion.forward(xys, sensor_to_frustum=True)

        return (
            float(xys[:, 0].min()),
            float(xys[:, 0].max()),
            float(xys[:, 1].min()),
            float(xys[:, 1].max()),
        )

    def precondition(self, cam_params):
        return self.precondition_mat_invs @ cam_params[:, :, None]

    def intrinsics_from_cam_params(self, cam_params: Tensor):
        # Compute preconditioned cam_params
        preconditioned_params = self.precondition(cam_params)

        intrinsics = preconditioned_params[:, 0 : self.num_intrinsics, 0]

        # multiplication focal length: f = f_init * exp(delta_f)
        intrinsics[:, 0:2] = self.intrinsics_init[:, 0:2] * torch.exp(intrinsics[:, 0:2])

        # pixel scale: multipley by focal length as well
        # cx = cx_init + fx_init * delta_cx
        intrinsics[:, 2:] = (self.intrinsics_init[:, 2:] + self.intrinsics_init[:, 0:2] * intrinsics[:, 2:])

        return intrinsics

    def rotations_from_cam_params(self, cam_params: Tensor):  # (num_images, 3, 3)
        # Compute preconditioned cam_params
        preconditioned_params = self.precondition(cam_params)

        Rt = se3_to_SE3(self.se3_init + preconditioned_params[:, self.num_intrinsics :, 0])
        return Rt[:, :, :3]

    def translations_from_cam_params(self, cam_params: Tensor) -> Tensor:  # (num_images, 3)
        # Compute preconditioned cam_params
        preconditioned_params = self.precondition(cam_params)

        Rt = se3_to_SE3(self.se3_init + preconditioned_params[:, self.num_intrinsics :, 0])
        return Rt[:, :, 3]

    @property
    def intrinsics(self):
        return self.intrinsics_from_cam_params(self.cam_params)

    @property
    def rotations(self):
        return self.rotations_from_cam_params(self.cam_params)

    @property
    def translations(self):
        return self.translations_from_cam_params(self.cam_params)

    def avg_intrinsics(self):
        return self.intrinsics.mean(dim=0)  # (4,)

    def intrinsic_variance(self, fxfy_weight: float, cxcy_weight: float):
        # Compute variance of each intrinsic parameter across each image
        fx_var, fy_var, cx_var, cy_var = torch.var(self.intrinsics, dim=0)  # (4,)
        
        # Return weighted average of fx/fy and cx/cy variances
        return fxfy_weight * (fx_var + fy_var) + cxcy_weight * (cx_var + cy_var)

    def project_points(
        self,
        points_3d: Tensor,  # (batch_size, 3)
        frame_inds: Tensor,  # (batch_size)
        cam_params: Optional[Tensor] = None, # (num_frames, num_params)
    ) -> Tensor:  # (batch_size, 2)
        # If no explicit camera parameters were supplied, use the camera groups's current params
        if cam_params is None:
            cam_params = self.cam_params

        # Get rotations and translations for the input frame indicies 
        rotations = self.rotations_from_cam_params(cam_params)[frame_inds, :, :] # (batch_size, 3, 3)
        translations = self.translations_from_cam_params(cam_params)[frame_inds, :] # (batch_size, 3)

        # Transform from world space to camera space
        points_frustum = (torch.einsum("ij,ikj->ik", points_3d, rotations) + translations) # (batch_size, 3)

        # Map from 3D to 2D normalized lens coordinates
        lens_coords = torch.stack(
            (
                points_frustum[:, 0] / points_frustum[:, 2], # X / Z
                points_frustum[:, 1] / points_frustum[:, 2]  # Y / Z
            ), dim=-1)  # (batch_size, 2)

        # Distort
        if self.distortion is not None:
            lens_coords = self.distortion.forward(lens_coords, sensor_to_frustum=False)  # (batch_size, 2)

        # Get intrinsics for the input frame indicies 
        intrinsics = self.intrinsics_from_cam_params(cam_params)[frame_inds, :] # (batch_size, 4)

        fx = intrinsics[:, 0]
        fy = intrinsics[:, 1]
        cx = intrinsics[:, 2]
        cy = intrinsics[:, 3]

        # Map lens coordinates to pixels
        pixel_coords = torch.stack(
            (
                lens_coords[:, 0] * fx + cx,
                lens_coords[:, 1] * fy + cy,
            ), dim=-1)  # (batch_size, 2)

        return pixel_coords
