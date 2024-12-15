# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

import unreal

import calibration
import calib_funcs
import cam_group
import distortion
import dataset
import calibration_utils
from importlib import reload

"""
Neural-Network based lens distortion solver 
"""
if (unreal.is_editor()):
    @unreal.uclass()
    class NeuralLensDistortionSolver(unreal.LensDistortionSolver):
        @unreal.ufunction(override=True)
        def solve(self, object_points_array, image_points_array, image_size, focal_length, image_center, init_distortion, camera_poses, target_poses, lens_model, pixel_aspect, solver_flags):
            reload(calibration_utils)
            reload(dataset)
            reload(distortion)
            reload(cam_group)
            reload(calib_funcs)
            reload(calibration)
            return calibration.solve(self, object_points_array, image_points_array, image_size, focal_length, image_center, init_distortion, camera_poses)
            
        @unreal.ufunction(override=True)
        def get_display_name(override=True):
            return unreal.Text("Neural Network Solver")

        @unreal.ufunction(override=True)
        def is_enabled(override=True):
            reload(calibration)
            return calibration.is_enabled()
