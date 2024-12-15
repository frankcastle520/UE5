# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

from typing import Tuple, Union, Optional, Callable, List

import torch
from torch import Tensor
import FrEIA

from nerfacc.cameras import _opencv_lens_distortion, _opencv_lens_undistortion

class LensDistortion(torch.nn.Module):
    # Base class implements no distortion
    def __init__(self):
        super().__init__()

    def forward(self, uv: Tensor, sensor_to_frustum: bool = True) -> Tensor:
        return uv

    def __str__(self):
        return "NoLensDistortion"

    def is_optimizable(self):
        return False

    def st_map(
        self,
        stmap_resolution: Tuple[int, int],
        image_resolution: Tuple[int, int],
        intrinsics: Tuple[float, float],
        uv_range: Optional[Tuple[float, float, float, float]] = None,
        sensor_to_frustum: bool = True,
    ) -> Tuple[Tensor, Tensor]:
        # Compute grid of UVs at the input ST Map resolution
        stmap_width, stmap_height = stmap_resolution
        image_width, image_height = image_resolution

        umin_in, umax_in, vmin_in, vmax_in = uv_range

        u_in, v_in = torch.meshgrid(
            torch.linspace(umin_in, umax_in, stmap_width),
            torch.linspace(vmin_in, vmax_in, stmap_height),
            indexing='ij'
        )

        uv_in = torch.stack((u_in, v_in), dim=2) # (stmap_width, stmap_height, 2)

        # Distort/Undistort the UVs
        # Note: A "distortion" ST map will store the undistorted UV at each texel, while an "undistortion" ST map will store the distorted UV
        uv_out = self.forward(uv=uv_in, sensor_to_frustum=sensor_to_frustum)

        # Compute the displacement
        uv_diff = uv_out - uv_in

        fx = intrinsics[0] / image_width
        fy = intrinsics[1] / image_height

        # Generate the regular grid of default UV values 
        xs, ys = torch.meshgrid(
            torch.linspace(0.0, ((stmap_width - 1) / stmap_width), stmap_width),
            torch.linspace(0.0, ((stmap_height - 1) / stmap_height), stmap_height),
            indexing='ij'
        )

        # Project uv_diff from normalized lens coordinates to normaled pixel (UV) space
        # Then add the positional value of each UV coordinate to the displacement to compute the ST map value
        st_map = torch.stack(
            (
                (uv_diff[:, :, 0] * fx) + xs, 
                (uv_diff[:, :, 1] * fy) + ys
            ), dim=2)

        return st_map

class OpenCV8ParamDistortion(LensDistortion):
    def __init__(self, params: List[float] = [0.0]):
        super().__init__()

        params_tensor = torch.tensor(params)

        # Pad to 8 parameters for nerfacc (k1, k2, p1, p2, k3, k4, k5, k6)
        if params_tensor.shape[-1] < 8:
            params_tensor = torch.nn.functional.pad(params_tensor, (0, 8 - params_tensor.shape[-1]), "constant", 0.0)

        self.params = params_tensor.flatten()
        self.undistort_eps = 1e-6
        self.undistort_iters = 10

    def forward(self, uv: Tensor, sensor_to_frustum: bool = True) -> Tensor:
        if sensor_to_frustum:  # Undistort
            result = _opencv_lens_undistortion(uv, self.params, self.undistort_eps, self.undistort_iters)
        else:  # Distort
            result = _opencv_lens_distortion(uv, self.params)

        return result

def init_weights_zero(m):
    if isinstance(m, torch.nn.Linear):
        m.weight.data.fill_(0.0)
        m.bias.data.fill_(0.0)

class NeuralLensDistortion(LensDistortion):
    # Adapted from iResNet from neuroLens.calibration.network.iRestNet to be a torch.nn.Module
    def __init__(
        self,
        num_nodes: int = 2,
        internal_size: int = 256,
        num_internal_layers: int = 4,
        inp_size_linear: int = 2,
    ):
        super().__init__()

        self.num_nodes = num_nodes
        self.internal_size = internal_size
        self.num_internal_layers = num_internal_layers
        self.inp_size_linear = inp_size_linear

        torch.manual_seed(0)
        nodes = [FrEIA.framework.graph_inn.InputNode(self.inp_size_linear, name="input")]

        for i in range(num_nodes):
            nodes.append(
                FrEIA.framework.graph_inn.Node(
                    nodes[-1],
                    FrEIA.modules.IResNetLayer,
                    {
                        "hutchinson_samples": 1,
                        "internal_size": internal_size,
                        "n_internal_layers": num_internal_layers,
                    },
                    conditions=[],
                    name=f"i_resnet_{i}",
                )
            )

        nodes.append(FrEIA.framework.graph_inn.OutputNode(nodes[-1], name="output"))

        self.i_resnet_linear = FrEIA.framework.GraphINN(nodes, verbose=False)

        for node in self.i_resnet_linear.node_list:
            if isinstance(node.module, FrEIA.modules.IResNetLayer):
                node.module.lipschitz_correction()
                init_weights_zero(node.module._modules["residual"][-1])

    def __str__(self):
        return (
            f"NeuralLensDistortion of {self.num_nodes} nodes"
            f" {self.num_internal_layers} internal layers of stmap_width {self.internal_size}."
        )

    def is_optimizable(self):
        return True

    def forward(self, uv: Tensor, sensor_to_frustum: bool = True) -> Tensor:
        if sensor_to_frustum:  # Undistort
            return self.i_resnet_linear(uv, jac=False)[0]
        else:  # Distort
            return self.i_resnet_linear(uv, rev=True, jac=False)[0]

    def test_inverse(self, batch_size=16, tol=1e-6):
        x = torch.randn(batch_size, self.inp_size_linear)
        x = x * torch.randn_like(x)
        x = x + torch.randn_like(x)

        y = self.i_resnet_linear(x, jac=False)[0]
        x_hat = self.i_resnet_linear(y, rev=True, jac=False)[0]

        print("Checking that inverse is close to input")
        assert torch.allclose(x, x_hat, atol=tol)

    def pretrain(
        self,
        distort_gt: Callable = lambda x, b: x,
        input_range: Tuple[float, float, float, float] = (-1, 1, -1, 1),
        focal_length: float = 1.0, # focal_length converts length to number of pixels
    ):
        # Set up pretraining optimizer and scheduler
        num_iterations = 1
        pretraining_lr = 1e-4

        pretraining_optimizer = torch.optim.Adam(self.i_resnet_linear.parameters(), lr=pretraining_lr)

        pretraining_scheduler = torch.optim.lr_scheduler.OneCycleLR(
            pretraining_optimizer,
            max_lr=pretraining_lr,
            total_steps=num_iterations,
        )

        # Generate scales and offsets to put random training points into the correct sensor range
        u_min, u_max, v_min, v_max = input_range

        scales = torch.tensor([[u_max - u_min, v_max - v_min]], dtype=torch.float32)
        offsets = torch.tensor([[u_min, v_min]], dtype=torch.float32)

        # Generate list of points to evaluate the network during pretraining
        grid_size_x, grid_size_y = (11, 11)
        eval_x, eval_y = torch.meshgrid(
            torch.linspace(u_min, u_max, grid_size_x),
            torch.linspace(v_min, v_max, grid_size_y),
            indexing='ij'
        )

        eval_points = torch.stack((eval_x.flatten(), eval_y.flatten()), dim=1) # (grid_size_x * grid_size_y, 2)

        batch_size = 512
        eval_every_n_steps = 100
        for index in range(num_iterations):
            # Generate random 2D points in range
            pts_frustum = torch.rand(batch_size, self.inp_size_linear) # (batch_size, 2)
            pts_frustum = pts_frustum * scales + offsets

            # Distort the random points using the distortion network we want to match (opencv)
            pts_sensor = distort_gt(pts_frustum)

            # Currently, noise is set to 0
            noise_ratio = 0
            noise = torch.rand_like(pts_sensor) * scales * noise_ratio
            pts_sensor_w_noise = pts_sensor + noise

            # Reset the gradients of all optimized tensors in the pretraining optimizer
            pretraining_optimizer.zero_grad()

            # Undistort the points that were distorted by the other distortion network
            pts_frustum_pred = self.forward(pts_sensor_w_noise, sensor_to_frustum=True)

            # Compute the diff between the undistorted points and the original random points (scaled by the focal length)
            loss = torch.nn.functional.smooth_l1_loss(pts_frustum_pred, pts_frustum) * focal_length # TODO: Why is this scaled by focal length?

            # Compute the gradient 
            loss.backward()

            # Try to mitigate against exploding gradients (prevent divergence)
            grad_max_norm = 0.5
            torch.nn.utils.clip_grad_norm_(self.i_resnet_linear.parameters(), max_norm=grad_max_norm)

            # Update the distortion network parameters
            pretraining_optimizer.step()

            # Update the learning rate
            pretraining_scheduler.step()

            # Evaluate the network using the eval points by measuring the current rmse
            if (index == 0 or (index + 1) % eval_every_n_steps == 0):
                # Distort the eval points using the network we want to match
                eval_pts_sensor = distort_gt(eval_points)

                # Distort the eval points using this network
                eval_pts_sensor_pred = self.forward(eval_points, sensor_to_frustum=False)

                # Compute the diff between the distorted points generated by the two networks (scaled by the focal length)
                mse = torch.nn.functional.mse_loss(eval_pts_sensor, eval_pts_sensor_pred) * (focal_length**2) # TODO: Why is this scaled by the focal length squared
                rmse = torch.sqrt(mse)

                print(f"pretrain iter={index:04d} loss={loss.item():0.4e} " f"eval_rmse={rmse.item():0.4f}")

