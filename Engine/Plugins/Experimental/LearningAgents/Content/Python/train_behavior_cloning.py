# -*- coding: utf-8 -*-
'''
Copyright Epic Games, Inc. All Rights Reserved.
'''

import sys
import os
import time
import json
import socket
import traceback
from collections import OrderedDict

import numpy as np
import torch
import torch.nn as nn
from nne_runtime_basic_cpu_pytorch import NeuralNetwork
from train_common import (
    Profile, 
    save_snapshot_to_file, 
    UE_RESPONSE_SUCCESS)

def train(config, communicator):

    # Action schema functions
    
    from train_common import schema_act_num, schema_entropy, schema_log_prob, schema_regularization

    training_name = config['TaskName']
    timestamp = config['TimeStamp']
    temp_directory = config['IntermediatePath']

    # For our default behavior cloning implementation, we will assume exactly one obs, action, etc.
    observation_schema = config['Schemas']['Observations'][0]['Schema']
    action_schema = config['Schemas']['Actions'][0]['Schema']
    act_enc_num = action_schema['EncodedSize']
    act_num = schema_act_num(action_schema)

    memory_state_num = int(config['MemoryStateNum'])
    
    bc_config = config['BehaviorCloningSettings']
    niterations = int(bc_config['IterationNum'])
    lr = bc_config['LearningRate']
    lr_gamma = bc_config['LearningRateDecay']
    weight_decay = bc_config['WeightDecay']
    batch_size = int(bc_config['BatchSize'])
    window = int(bc_config['Window'])
    action_dist_weight = 1.0
    action_reg_weight = bc_config['ActionRegularizationWeight']
    action_ent_weight = bc_config['ActionEntropyWeight']
    seed = int(bc_config['Seed'])

    if bc_config['Device'] == 'GPU':
        if torch.cuda.is_available():
            device = 'cuda'
        else:
            if config['LoggingEnabled']:
                print('Warning: GPU does not support CUDA. Defaulting to CPU training.')
            device = 'cpu'
    elif bc_config['Device'] == 'CPU':
        device = 'cpu'
    else:
        if config['LoggingEnabled']:
            print('Warning: Unknown training device "%s". Defaulting to CPU training.' % bc_config['Device'])
        device = 'cpu'

    use_tensorboard = bc_config['UseTensorBoard']
    save_snapshots = bc_config['SaveSnapshots']
    iterations_per_sync = 10

    log_enabled = config['LoggingEnabled']

    # Import TensorBoard
    if use_tensorboard:
        try:
            from torch.utils.tensorboard import SummaryWriter
        except ImportError as e:
            if log_enabled:
                print('Warning: Failed to Load TensorBoard: %s. Please add manually to site-packages.' % str(e))
            bc_config['UseTensorBoard'] = False
            use_tensorboard = False

    if log_enabled:
        print(json.dumps(config, indent=4))
        sys.stdout.flush()

    training_identifier = training_name + '_bc_' + communicator.name + '_' + timestamp

    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.set_num_threads(1)

    # Receive initial policy
    policy_network_id = 0
    policy_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Policy...')
    response = communicator.receive_network(policy_network_id, policy_network)
    assert response == UE_RESPONSE_SUCCESS

    # Receive initial encoder
    encoder_network_id = 1
    encoder_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Encoder...')
    response = communicator.receive_network(encoder_network_id, encoder_network)
    assert response == UE_RESPONSE_SUCCESS

    # Receive initial decoder
    decoder_network_id = 2
    decoder_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Decoder...')
    response = communicator.receive_network(decoder_network_id, decoder_network)
    assert response == UE_RESPONSE_SUCCESS
    
    # Wait for experience
    replay_buffer_id = 0
    with Profile('Waiting for Experience...', log_enabled):
        response, buffer, _ = communicator.receive_experience(
            replay_buffer_id,
            trim_episode_start=0, 
            trim_episode_end=0)
        
        episode_starts = buffer['starts']
        episode_lengths = buffer['lengths']
        observations = buffer['obs']
        actions = buffer['act']

    # Create Optimizer

    if log_enabled: print('Creating Optimizer...')

    optimizer_policy = torch.optim.AdamW(
        list(policy_network.parameters()) + 
        list(encoder_network.parameters()) +
        list(decoder_network.parameters()),
        lr=lr,
        amsgrad=True,
        weight_decay=weight_decay)

    scheduler_policy = torch.optim.lr_scheduler.ExponentialLR(optimizer_policy, gamma=lr_gamma)

    if use_tensorboard:
        if log_enabled: print('Opening TensorBoard...')
        writer = SummaryWriter(log_dir=temp_directory + "/TensorBoard/runs/" + training_identifier)

    # Create Batches
    
    window_indices = []
    window_masks = []
    
    for ei in range(len(episode_starts)):
        for si in range(episode_starts[ei], episode_starts[ei] + episode_lengths[ei]):
            if si + window <= len(observations):
                window_indices.append(np.arange(si, si + window))
                window_masks.append(np.arange(si, si + window) < episode_starts[ei] + episode_lengths[ei])

    window_indices = np.array(window_indices, dtype=np.int64)
    window_masks = np.array(window_masks, dtype=bool)

    window_indices = torch.as_tensor(window_indices, dtype=torch.long, device=device)
    window_masks = torch.as_tensor(window_masks, dtype=torch.bool, device=device)

    # Upload Tensors

    observations = torch.as_tensor(observations, dtype=torch.float32, device=device)
    actions = torch.as_tensor(actions, dtype=torch.float32, device=device)
    
    # Training Loop

    if log_enabled: print('Begin Training...')

    rolling_avg_loss = None
    ti = 0

    while True:

        # Check if we need to stop

        if communicator.has_stop():
            response = communicator.receive_stop()
            assert response == UE_RESPONSE_SUCCESS
            break
        
        # Do multiple iterations per sync
        
        for _ in range(iterations_per_sync):
        
            # Check for completion

            if ti >= niterations:
                response = communicator.send_complete() 
                assert response == UE_RESPONSE_SUCCESS
                break

            # Update Policy

            with Profile('Training', log_enabled):
                
                optimizer_policy.zero_grad()

                # Gather Batch
                
                batch = torch.randint(0, len(window_indices), size=[batch_size])
                batch_mask = window_masks[batch]
                batch_obs = observations[window_indices[batch]]
                batch_act = actions[window_indices[batch]]
                
                # Compute Loss

                loss_dist = 0.0
                loss_ent = 0.0
                loss_reg = 0.0
                
                policy_mem = torch.zeros([batch_size, memory_state_num], dtype=torch.float, device=device)
                
                for i in range(window):
                    
                    act_enc_policy_mem = policy_network(torch.cat([encoder_network(batch_obs[:,i]), policy_mem], dim=-1))
                    act_enc, policy_mem = act_enc_policy_mem[:,:act_enc_num],  act_enc_policy_mem[:,act_enc_num:]
                    act_dist = decoder_network(act_enc)
                    
                    loss_dist += action_dist_weight * (batch_mask[:,i] * -(schema_log_prob(action_schema, act_dist, batch_act[:,i]) / act_num)).mean() / window
                    loss_ent += action_ent_weight * (batch_mask[:,i] * -(schema_entropy(action_schema, act_dist) / act_num)).mean() / window
                    loss_reg += action_reg_weight * (batch_mask[:,i] * (schema_regularization(action_schema, act_dist) / act_num)).mean() / window
                
                loss = loss_dist + loss_ent + loss_reg
                
                # Update Weights

                loss.backward()
                optimizer_policy.step()
            
            communicator.send_ping()
            
            # Log stats

            with Profile('Logging', log_enabled):

                if rolling_avg_loss is None:
                    rolling_avg_loss = loss.item()
                else:
                    rolling_avg_loss = rolling_avg_loss * 0.99 + loss.item() * 0.01

                if log_enabled: print('\rIter: %7i Loss: %7.5f' % (ti, rolling_avg_loss))
                sys.stdout.flush()
                
                # TensorBoard

                if use_tensorboard:
                    writer.add_scalar('loss/loss', loss.item(), ti)
                    writer.add_scalar('loss/dist', loss_dist.item(), ti)
                    writer.add_scalar('loss/ent', loss_ent.item(), ti)
                    writer.add_scalar('loss/reg', loss_reg.item(), ti)

            # Write Snapshot
            
            if save_snapshots and ti % 1000 == 0:
                
                if not os.path.exists(temp_directory + "/Snapshots/"):
                    os.mkdir(temp_directory + "/Snapshots/")
                
                policy_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_policy_' + str(ti) + '.bin'
                encoder_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_encoder_' + str(ti) + '.bin'
                decoder_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_decoder_' + str(ti) + '.bin'
                
                save_snapshot_to_file(policy_network, policy_snapshot)
                save_snapshot_to_file(encoder_network, encoder_snapshot)
                save_snapshot_to_file(decoder_network, decoder_snapshot)
                
                if log_enabled: 
                    print('Saved Policy Snapshot to: "%s"' % policy_snapshot)
                    print('Saved Encoder Snapshot to: "%s"' % encoder_snapshot)
                    print('Saved Decoder Snapshot to: "%s"' % decoder_snapshot)
            
            ti += 1

            # Update lr schedulers

            if ti % 1000 == 0:
                scheduler_policy.step()

        # Push Networks

        with Profile('Pushing Policy...', log_enabled):
            response = communicator.send_network(0, policy_network) 
            assert response == UE_RESPONSE_SUCCESS

        with Profile('Pushing Encoder...', log_enabled):
            response = communicator.send_network(1, encoder_network) 
            assert response == UE_RESPONSE_SUCCESS

        with Profile('Pushing Decoder...', log_enabled):
            response = communicator.send_network(2, decoder_network) 
            assert response == UE_RESPONSE_SUCCESS

    if log_enabled: print("Done!")
