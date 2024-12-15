# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""
import unreal as ue
import torch
from torch.utils.data import DataLoader


def extract_morph_targets(morph_tensor,
                          training_model,
                          output_mean,
                          output_std,
                          network,
                          inputs,
                          training_set,
                          dataset):
    """Extract the morph targets from the trained neural network.
    This assumes the last layers contains the deltas.
    The last layer should contain xyzxyzxyz style values.
    This will pass the deltas to the morph model's c++ class. 
    It will also store the absolute max weight value of each morph target as seen during training.  

    Keyword arguments:
        morph_tensor    -- The morph target tensor, it must be of size (num_delta_floats_per_morph, num_morphs) 
        training_model  -- The ML Deformer training model object.
        output_mean     -- The means of the training target values (the deltas).
        output_std      -- The standard deviation of the training target values (the deltas).
        network         -- The pytorch trained model.
        inputs          -- The training inputs, so like the input rotations and curve values for each frame.
        training_set    -- The training dataset.
        dataset         -- The full dataset object, before splitting into train and test set.
    """
    with ue.ScopedSlowTask(2, 'Extracting Morph Targets') as morph_task:
        morph_task.make_dialog(True)

        # Pre-multiply the morph target deltas with the standard deviation.
        morph_target_matrix = morph_tensor * output_std.unsqueeze(dim=-1)
        morph_task.enter_progress_frame(1)
        num_morph_targets = morph_target_matrix.shape[-1] + 1  # Add one, because we add the means as well.
        print('Num Morph Targets: ', num_morph_targets)

        # Store the means as first morph target, then add the generated morphs to it.
        deltas = output_mean.cpu().detach().numpy().tolist()
        deltas.extend(morph_target_matrix.T.flatten().cpu().detach().numpy().tolist())
        training_model.get_model().set_morph_target_delta_floats(deltas)

        morph_task.enter_progress_frame(1)

    # Save the minimum and maximum weight of each morph target, over all training samples. 
    save_morphs_minmax_weights(training_model, network, inputs, training_set, dataset)


def save_morphs_minmax_weights(training_model, network, inputs, training_set, dataset):
    """Update the model with the min and max morph target weight values, over all samples in the training data set.
    Each morph target will have a minimum and maximum weight value.    

    Keyword arguments:
        training_model  -- The ML Deformer training model object.
        network         -- The pytorch trained model.
        inputs          -- The training inputs, so like the input rotations and curve values for each frame.
        training_set    -- The training dataset.
        dataset         -- The full dataset object, before splitting into train and test set.
    """
    # Create a data loader that contains all samples.
    # And then sample the network with all those inputs, to get the morph target weights.
    batch_size = 128
    dataloader = DataLoader(training_set, batch_size=batch_size, shuffle=False, collate_fn=dataset.collate)
    num_iters = len(dataloader)
    network.training = False
    
    with ue.ScopedSlowTask(num_iters, 'Extracting morph target weight bounds') as morph_task:
        morph_task.make_dialog(can_cancel=False)
        for iter_index, (x, y, _) in enumerate(dataloader):
            outputs = network(x)

            # Calculate the min and max weight of each morph target, in the training data, for this iteration.
            min_values, _ = torch.min(outputs, 0)
            max_values, _ = torch.max(outputs, 0)

            # Keep track of the min and max over all iterations.
            final_min_values = torch.minimum(final_min_values, min_values) if iter_index > 0 else min_values              
            final_max_values = torch.maximum(final_max_values, max_values) if iter_index > 0 else max_values

            morph_task.enter_progress_frame(1)

    network.training = True

    # Allow the weights to go slightly out of bounds.
    scale = 1.05
    final_min_values = final_min_values * scale 
    final_max_values = final_max_values * scale 

    # Pass the actual min and max morph weight values to the model.
    training_model.get_model().set_morph_targets_min_max_weights(
        final_min_values.flatten().cpu().detach().numpy().tolist(),
        final_max_values.flatten().cpu().detach().numpy().tolist())
