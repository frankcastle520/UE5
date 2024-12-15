# -*- coding: utf-8 -*-
'''
Copyright Epic Games, Inc. All Rights Reserved.
'''

import json
import numpy as np
import sys
import torch
import os

from nne_runtime_basic_cpu_pytorch import NeuralNetwork
from ppo import PPOTrainer
from train_common import (
    Profile,
    save_snapshot_to_file,
    UE_RESPONSE_SUCCESS,
    UE_RESPONSE_STOPPED)

def train(config, communicator):

    training_name = config['TaskName']
    timestamp = config['TimeStamp']
    temp_directory = config['IntermediatePath']
    
    # For our default PPO implementation, we will assume exactly one obs, action, etc.
    observation_schema = config['Schemas']['Observations'][0]['Schema']
    action_schema = config['Schemas']['Actions'][0]['Schema']

    ppo_config = config['PPOSettings']
    niterations = int(ppo_config['IterationNum'])
    lr_policy = ppo_config['LearningRatePolicy']
    lr_critic = ppo_config['LearningRateCritic']
    lr_gamma = ppo_config['LearningRateDecay']
    weight_decay = ppo_config['WeightDecay']
    policy_batch_size = int(ppo_config['PolicyBatchSize'])
    critic_batch_size = int(ppo_config['CriticBatchSize'])
    policy_window = int(ppo_config['PolicyWindow'])
    iterations_per_gather = int(ppo_config['IterationsPerGather'])
    iterations_critic_warmup = int(ppo_config['CriticWarmupIterations'])
    eps_clip = ppo_config['EpsilonClip']
    action_surr_weight = ppo_config['ActionSurrogateWeight']
    action_reg_weight = ppo_config['ActionRegularizationWeight']
    action_ent_weight = ppo_config['ActionEntropyWeight']
    return_reg_weight = ppo_config['ReturnRegularizationWeight']
    gae_lambda = ppo_config['GaeLambda']
    advantage_normalization = ppo_config['AdvantageNormalization']
    advantage_min = ppo_config['AdvantageMin']
    advantage_max = ppo_config['AdvantageMax']
    use_grad_norm_max_clipping = ppo_config['UseGradNormMaxClipping']
    grad_norm_max = ppo_config['GradNormMax']
    trim_episode_start = int(ppo_config['TrimEpisodeStartStepNum'])
    trim_episode_end = int(ppo_config['TrimEpisodeEndStepNum'])
    seed = int(ppo_config['Seed'])
    discount_factor = ppo_config['DiscountFactor']
    
    if ppo_config['Device'] == 'GPU':
        if torch.cuda.is_available():
            device = 'cuda'
        else:
            if config['LoggingEnabled']:
                print('Warning: GPU does not support CUDA. Defaulting to CPU training.')
            device = 'cpu'
    elif ppo_config['Device'] == 'CPU':
        device = 'cpu'
    else:
        if config['LoggingEnabled']:
            print('Warning: Unknown training device "%s". Defaulting to CPU training.' % ppo_config['Device'])
        device = 'cpu'
    
    use_tensorboard = ppo_config['UseTensorBoard']

    save_snapshots = ppo_config['SaveSnapshots']

    log_enabled = config['LoggingEnabled']

    # Import TensorBoard
    if use_tensorboard:
        try:
            from torch.utils.tensorboard import SummaryWriter
        except ImportError as e:
            if log_enabled:
                print('Warning: Failed to Load TensorBoard: %s. Please add manually to site-packages.' % str(e))
            ppo_config['UseTensorBoard'] = False
            use_tensorboard = False

    if log_enabled:
        print(json.dumps(config, indent=4))
        sys.stdout.flush()

    training_identifier = training_name + '_ppo_' + communicator.name + '_' + timestamp

    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.set_num_threads(1)

    # Receive initial policy
    policy_network_id = 0
    policy_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Policy...')
    response = communicator.receive_network(policy_network_id, policy_network)
    assert response == UE_RESPONSE_SUCCESS

    # Receive initial critic
    critic_network_id = 1
    critic_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Critic...')
    response = communicator.receive_network(critic_network_id, critic_network)
    assert response == UE_RESPONSE_SUCCESS
    
    # Receive initial encoder
    encoder_network_id = 2
    encoder_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Encoder...')
    response = communicator.receive_network(encoder_network_id, encoder_network)
    assert response == UE_RESPONSE_SUCCESS

    # Receive initial decoder
    decoder_network_id = 3
    decoder_network = NeuralNetwork(device=device)
    if log_enabled: print('Receiving Decoder...')
    response = communicator.receive_network(decoder_network_id, decoder_network)
    assert response == UE_RESPONSE_SUCCESS
    
    # Create Optimizer
    if log_enabled: print('Creating Optimizer...')

    optimizer_policy = torch.optim.AdamW(
        list(policy_network.parameters()) + 
        list(encoder_network.parameters()) +
        list(decoder_network.parameters()),
        lr=lr_policy,
        amsgrad=True,
        weight_decay=weight_decay)

    optimizer_critic = torch.optim.AdamW(
        list(critic_network.parameters()) +
        list(encoder_network.parameters()),
        lr=lr_critic,
        amsgrad=True,
        weight_decay=weight_decay)

    scheduler_policy = torch.optim.lr_scheduler.ExponentialLR(optimizer_policy, gamma=lr_gamma)
    scheduler_critic = torch.optim.lr_scheduler.ExponentialLR(optimizer_critic, gamma=lr_gamma)

    # Create PPO Policy
    if log_enabled: print('Creating PPO Policy...')

    ppo_trainer = PPOTrainer(
        observation_schema,
        action_schema,
        policy_network,
        critic_network,
        encoder_network,
        decoder_network,
        optimizer_policy,
        optimizer_critic,
        discount_factor=discount_factor,
        gae_lambda=gae_lambda,
        eps_clip=eps_clip,
        advantage_normalization=advantage_normalization,
        advantage_min=advantage_min,
        advantage_max=advantage_max,
        use_grad_norm_max_clipping=use_grad_norm_max_clipping,
        grad_norm_max=grad_norm_max,
        action_surr_weight=action_surr_weight,
        action_reg_weight=action_reg_weight,
        action_ent_weight=action_ent_weight,
        return_reg_weight=return_reg_weight)

    if use_tensorboard:
        if log_enabled: print('Opening TensorBoard...')
        writer = SummaryWriter(log_dir=temp_directory + "/TensorBoard/runs/" + training_identifier, max_queue=1000)

    # Training Loop

    if log_enabled: print('Begin Training...')

    ti = 0
    replay_buffer_id = 0

    while True:

        # Pull Experience
        with Profile('Pull Experience', log_enabled):
            response, buffer, exp_stats = communicator.receive_experience(
                replay_buffer_id,
                trim_episode_start, 
                trim_episode_end)
            
        if response == UE_RESPONSE_STOPPED:
            break
        else:
            assert response == UE_RESPONSE_SUCCESS
            avg_reward = exp_stats['experience/avg_reward'][0]
            avg_reward_sum = exp_stats['experience/avg_reward_sum'][0]
            avg_episode_length = exp_stats['experience/avg_episode_length']

        # Check for completion
        if ti >= niterations:
            response = communicator.send_complete()
            assert response == UE_RESPONSE_SUCCESS
            break
        
        # Buffer Manipulation
        # Our PPO implementation expects one of each of the data buffers
        buffer['obs'] = buffer['obs'][0]
        buffer['obs_next'] = buffer['obs_next'][0]
        buffer['act'] = buffer['act'][0]
        buffer['mem'] = buffer['mem'][0]
        buffer['mem_next'] = buffer['mem_next'][0]
        buffer['rew'] = buffer['rew'][0].squeeze()

        # Train
        with Profile('Training', log_enabled):
            
            if ti == 0 and iterations_critic_warmup > 0:
                ppo_trainer.warmup_critic(
                    buffer=buffer,
                    critic_batch_size=critic_batch_size,
                    recompute_returns_iterations=iterations_critic_warmup,
                    update_func=lambda: communicator.send_ping())
            
            stats = ppo_trainer.train(
                buffer=buffer, 
                policy_batch_size=policy_batch_size,
                critic_batch_size=critic_batch_size,
                iterations=iterations_per_gather, 
                policy_window=policy_window,
                update_func=lambda: communicator.send_ping())
                
            avg_return = stats['experience/avg_return']
            
        # Push Policy

        with Profile('Pushing Policy', log_enabled):
            response = communicator.send_network(policy_network_id, policy_network)
            assert response == UE_RESPONSE_SUCCESS
            
        # Push Critic
        
        with Profile('Pushing Critic', log_enabled):
            response = communicator.send_network(critic_network_id, critic_network)
            assert response == UE_RESPONSE_SUCCESS
            
        # Push Encoder
        
        with Profile('Pushing Encoder', log_enabled):
            response = communicator.send_network(encoder_network_id, encoder_network)
            assert response == UE_RESPONSE_SUCCESS
            
        # Push Decoder
        
        with Profile('Pushing Decoder', log_enabled):
            response = communicator.send_network(decoder_network_id, decoder_network)
            assert response == UE_RESPONSE_SUCCESS 
            
        # Log stats

        with Profile('Logging', log_enabled):

            if log_enabled: print('\rIter: %7i | Avg Rew: %7.5f | Avg Rew Sum: %7.5f | Avg Return: %7.5f | Avg Episode Len: %7.5f' % 
                (ti, avg_reward, avg_reward_sum, avg_return, avg_episode_length))
            sys.stdout.flush()
            
            if use_tensorboard:
                writer.add_scalar('experience/avg_reward', avg_reward, ti)
                writer.add_scalar('experience/avg_reward_sum', avg_reward_sum, ti)
                writer.add_scalar('experience/avg_return', avg_return, ti)
                writer.add_scalar('experience/avg_episode_length', avg_episode_length, ti)

            for bi in range(len(stats['loss/policy'])):

                if use_tensorboard:
                    writer.add_scalar('grads/encoder', stats['grads/encoder'][bi], ti)
                    writer.add_scalar('grads/decoder', stats['grads/decoder'][bi], ti)
                    writer.add_scalar('grads/policy', stats['grads/policy'][bi], ti)
                    writer.add_scalar('grads/critic', stats['grads/critic'][bi], ti)
                    writer.add_scalar('loss/critic_ret', stats['loss/critic_ret'][bi], ti)
                    writer.add_scalar('loss/critic_reg', stats['loss/critic_reg'][bi], ti)
                    writer.add_scalar('loss/critic', stats['loss/critic'][bi], ti)
                    writer.add_scalar('loss/policy_surr', stats['loss/policy_surr'][bi], ti)
                    writer.add_scalar('loss/policy_reg', stats['loss/policy_reg'][bi], ti)
                    writer.add_scalar('loss/policy_ent', stats['loss/policy_ent'][bi], ti)
                    writer.add_scalar('loss/policy', stats['loss/policy'][bi], ti)
                
                # Write Snapshot
                
                if save_snapshots and ti % 1000 == 0:
                    
                    if not os.path.exists(temp_directory + "/Snapshots/"):
                        os.mkdir(temp_directory + "/Snapshots/")
                    
                    policy_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_policy_' + str(ti) + '.bin'
                    critic_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_critic_' + str(ti) + '.bin'
                    encoder_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_encoder_' + str(ti) + '.bin'
                    decoder_snapshot = temp_directory + "/Snapshots/" + training_identifier + '_decoder_' + str(ti) + '.bin'
                    
                    save_snapshot_to_file(policy_network, policy_snapshot)
                    save_snapshot_to_file(critic_network, critic_snapshot)
                    save_snapshot_to_file(encoder_network, encoder_snapshot)
                    save_snapshot_to_file(decoder_network, decoder_snapshot)
                    
                    if log_enabled:
                        print('Saved Policy Snapshot to: "%s"' % policy_snapshot)
                        print('Saved Critic Snapshot to: "%s"' % critic_snapshot)
                        print('Saved Encoder Snapshot to: "%s"' % encoder_snapshot)
                        print('Saved Decoder Snapshot to: "%s"' % decoder_snapshot)
                    
                ti += 1
                
                # Update lr schedulers
                
                if ti % 1000 == 0:
                    scheduler_policy.step()
                    scheduler_critic.step()
        
    if log_enabled: print("Done!")
