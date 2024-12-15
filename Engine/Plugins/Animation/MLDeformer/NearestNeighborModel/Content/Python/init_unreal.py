# -*- coding: utf-8 -*-
"""
NearestNeighborTrainingModel class.

Copyright Epic Games, Inc. All Rights Reserved.
"""

from importlib import reload
import mldeformer.training_helpers
import unreal

@unreal.uclass()
class NearestNeighborPythonTrainingModel(unreal.NearestNeighborTrainingModel):
    def import_module(self):
        import nearestneighbormodel
        reload(nearestneighbormodel)
        return nearestneighbormodel
    
    @unreal.ufunction(override=True)
    def update_available_devices(self):
        reload(mldeformer.training_helpers)
        mldeformer.training_helpers.update_training_device_list(self)
    
    @unreal.ufunction(override=True)
    def train(self):
        nearestneighbormodel = self.import_module()
        return nearestneighbormodel.train(self)

    @unreal.ufunction(override=True)
    def update_nearest_neighbor_data(self):
        nearestneighbormodel = self.import_module()
        return nearestneighbormodel.update_nearest_neighbor_data(self)
    
    @unreal.ufunction(override=True)
    def kmeans_cluster_poses(self, data):
        nearestneighbormodel = self.import_module()
        return nearestneighbormodel.kmeans_cluster_poses(self, data)
    
    @unreal.ufunction(override=True)
    def get_neighbor_stats(self, data):
        nearestneighbormodel = self.import_module()
        return nearestneighbormodel.get_neighbor_stats(self, data)

@unreal.uclass()
class NearestNeighborPythonOptimizedNetworkLoader(unreal.NearestNeighborOptimizedNetworkLoader):
    @unreal.ufunction(override=True)
    def load_optimized_network(self, onnx_path):
        return False
