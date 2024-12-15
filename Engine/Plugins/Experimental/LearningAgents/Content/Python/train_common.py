# -*- coding: utf-8 -*-
'''
Copyright Epic Games, Inc. All Rights Reserved.
'''

import json
import time
import struct
import select

from collections import OrderedDict
from train_shared_memory import SharedMemory

import numpy as np
import torch
import torch.nn.functional as F

UE_LEARNING_DEVELOPMENT = False
UE_LEARNING_PROFILE = True


# Profile

class Profile:

    def __init__(self, name, enabled=True):
        self.name = name
        self.enabled = enabled and UE_LEARNING_PROFILE

    def __enter__(self):
        if self.enabled:
            self.start_time = time.time()

    def __exit__(self, type, value, traceback):
        if self.enabled:
            print("Profile| %-25s %6ims" % (self.name, round((time.time() - self.start_time) * 1000)))


# Network Snapshots

# Magic Number of Neural Network Snapshots. Should match what is given in LearningNeuralNetwork.cpp
UE_LEARNING_NEURAL_NETWORK_MAGIC_NUMBER = 0x1e9b0c80

# Version Number of Neural Network Snapshots. Should match what is given in LearningNeuralNetwork.cpp
UE_LEARNING_NEURAL_NETWORK_VERSION_NUMBER = 1

# Functions for loading and saving snapshot from binary data into a python neural network object

def load_snapshot(data, network):

    magic = struct.unpack("I", data[0:4].tobytes())[0]
    assert magic == UE_LEARNING_NEURAL_NETWORK_MAGIC_NUMBER
    version = struct.unpack("I", data[4:8].tobytes())[0]
    assert version == UE_LEARNING_NEURAL_NETWORK_VERSION_NUMBER
    input_num = struct.unpack("I", data[8:12].tobytes())[0]
    output_num = struct.unpack("I", data[12:16].tobytes())[0]
    compatibility_hash = struct.unpack("I", data[16:20].tobytes())[0]
    network_filedata_size = struct.unpack("I", data[20:24].tobytes())[0]

    # For convenience we just attach these additional properties to the network object
    network.load_from_filedata(data[24:])
    network.input_num = input_num
    network.output_num = output_num
    network.compatibility_hash = compatibility_hash
    
def save_snapshot(data, network):
    data[ 0: 4] = np.frombuffer(struct.pack("I", UE_LEARNING_NEURAL_NETWORK_MAGIC_NUMBER), np.uint8)
    data[ 4: 8] = np.frombuffer(struct.pack("I", UE_LEARNING_NEURAL_NETWORK_VERSION_NUMBER), np.uint8)
    data[ 8:12] = np.frombuffer(struct.pack("I", network.input_num), np.uint8)
    data[12:16] = np.frombuffer(struct.pack("I", network.output_num), np.uint8)
    data[16:20] = np.frombuffer(struct.pack("I", network.compatibility_hash), np.uint8)
    data[20:24] = np.frombuffer(struct.pack("I", network.get_filedata_size()), np.uint8)
    network.save_to_filedata(data[24:])

def get_snapshot_byte_num(network):
    return (4 + 4 + 4 + 4 + 4 + 4 + network.get_filedata_size())

def save_snapshot_to_file(network, filename):
    with open(filename, 'wb') as f:
        data = np.zeros([get_snapshot_byte_num(network)], dtype=np.uint8)
        save_snapshot(data, network)
        f.write(data.tobytes())
    

# Completion

UE_COMPLETION_RUNNING = 0
UE_COMPLETION_TRUNCATED = 1
UE_COMPLETION_TERMINATED = 2


# Trainer Response

UE_RESPONSE_SUCCESS = 0
UE_RESPONSE_UNEXPECTED = 1
UE_RESPONSE_COMPLETED = 2
UE_RESPONSE_STOPPED = 3
UE_RESPONSE_TIMEOUT = 4


# Shared Memory

UE_SHARED_MEMORY_EXPERIENCE_EPISODE_NUM     = 0
UE_SHARED_MEMORY_EXPERIENCE_STEP_NUM        = 1
UE_SHARED_MEMORY_EXPERIENCE_SIGNAL          = 2
UE_SHARED_MEMORY_CONFIG_SIGNAL              = 3
UE_SHARED_MEMORY_NETWORK_SIGNAL             = 4
UE_SHARED_MEMORY_COMPLETE_SIGNAL            = 5
UE_SHARED_MEMORY_STOP_SIGNAL                = 6
UE_SHARED_MEMORY_PING_SIGNAL                = 7
UE_SHARED_MEMORY_NETWORK_ID                 = 8
UE_SHARED_MEMORY_REPLAY_BUFFER_ID           = 9

UE_SHARED_MEMORY_CONTROL_NUM                = 10



def shared_memory_map_array(guid, shape, dtype):
    size = np.product(shape) * np.dtype(dtype).itemsize
    if size > 0:
        handle = SharedMemory(guid, create=False, size=size)
        assert handle is not None
        array = np.frombuffer(handle.buf, dtype=dtype, count=np.product(shape)).reshape(shape)
        return handle, array
    else:
        return None, np.empty(shape, dtype=dtype)
        

def shared_memory_send_network(controls, buffer, network_id, network):

    while controls[UE_SHARED_MEMORY_NETWORK_SIGNAL]:
        time.sleep(0.001)
    
    save_snapshot(buffer, network)

    controls[UE_SHARED_MEMORY_NETWORK_ID] = network_id
    controls[UE_SHARED_MEMORY_NETWORK_SIGNAL] = 1
    
    return UE_RESPONSE_SUCCESS

def shared_memory_send_network_multiprocess(processes_controls, buffer, network_id, network):

    for controls in processes_controls:
        while controls[UE_SHARED_MEMORY_NETWORK_SIGNAL]:
            time.sleep(0.001)
    
    save_snapshot(buffer, network)

    for controls in processes_controls:
        controls[UE_SHARED_MEMORY_NETWORK_ID] = network_id
        controls[UE_SHARED_MEMORY_NETWORK_SIGNAL] = 1
    
    return UE_RESPONSE_SUCCESS


def shared_memory_wait_for_config(controls):
   
    while not controls[0, UE_SHARED_MEMORY_CONFIG_SIGNAL]:
        time.sleep(0.001)
    
    return UE_RESPONSE_SUCCESS

def shared_memory_receive_network(controls, buffer, network_id, network):

    controls[UE_SHARED_MEMORY_NETWORK_SIGNAL] = 1
    
    while controls[UE_SHARED_MEMORY_NETWORK_SIGNAL]:
        time.sleep(0.001)

    if network_id != controls[UE_SHARED_MEMORY_NETWORK_ID]:
        return UE_RESPONSE_UNEXPECTED
    
    load_snapshot(buffer, network)
    
    return UE_RESPONSE_SUCCESS


def shared_memory_receive_network_multiprocess(processes_controls, buffer, network_id, network):
    
    for controls in processes_controls:
        controls[UE_SHARED_MEMORY_NETWORK_SIGNAL] = 1
    
    for controls in processes_controls:
        while controls[UE_SHARED_MEMORY_NETWORK_SIGNAL]:
            time.sleep(0.001)
    
    for controls in processes_controls:
        if network_id != controls[UE_SHARED_MEMORY_NETWORK_ID]:
            print('Invalid network id, expected %d received %d' % (network_id, controls[UE_SHARED_MEMORY_NETWORK_ID]))
            return UE_RESPONSE_UNEXPECTED

    load_snapshot(buffer, network)

    return UE_RESPONSE_SUCCESS


def shared_memory_receive_experience_multiprocess(control, replay_buffer_id, replay_buffer, trim_episode_start, trim_episode_end):
    
    process_num = replay_buffer.process_num
    processes_controls = [control[1][pi] for pi in range(process_num)]

    # Wait until experience is ready
    for controls in processes_controls:
        while not controls[UE_SHARED_MEMORY_EXPERIENCE_SIGNAL]:
        
            if controls[UE_SHARED_MEMORY_STOP_SIGNAL]:
                controls[UE_SHARED_MEMORY_STOP_SIGNAL] = 0
                return UE_RESPONSE_STOPPED, None, None
        
            time.sleep(0.001)
    
    # Check buffer ids match
    for controls in processes_controls:
        if controls[UE_SHARED_MEMORY_REPLAY_BUFFER_ID] != replay_buffer_id:
            print('Invalid replay buffer id, expected %d received %d' % (replay_buffer_id, controls[UE_SHARED_MEMORY_REPLAY_BUFFER_ID]))
            return UE_RESPONSE_UNEXPECTED, None, None

    # Reshape data from buffer
    processes_episode_starts = [replay_buffer.episode_starts[1][pi] for pi in range(process_num)]
    processes_episode_lengths = [replay_buffer.episode_lengths[1][pi] for pi in range(process_num)]

    if replay_buffer.has_completions:
        processes_episode_completion_modes = [replay_buffer.episode_completion_modes[1][pi] for pi in range(process_num)]
    
    processes_episode_final_observations = [[episode_final_observations[1][pi] for episode_final_observations in replay_buffer.episode_final_observations] for pi in range(process_num)]
    processes_episode_final_memory_states = [[episode_final_memory_states[1][pi] for episode_final_memory_states in replay_buffer.episode_final_memory_states] for pi in range(process_num)]
    processes_observations = [[observations[1][pi] for observations in replay_buffer.observations] for pi in range(process_num)]
    processes_actions = [[actions[1][pi] for actions in replay_buffer.actions] for pi in range(process_num)]
    processes_memory_states = [[memory_states[1][pi] for memory_states in replay_buffer.memory_states] for pi in range(process_num)]
    processes_rewards = [[rewards[1][pi] for rewards in replay_buffer.rewards] for pi in range(process_num)]

    # Append experience from shared memory to replay buffer

    avg_rewards = [0.0 for _ in replay_buffer.rewards]
    avg_reward_sums = [0.0 for _ in replay_buffer.rewards]
    avg_episode_length = 0.0
    total_episode_num = 0

    # Compute Buffer Size
    
    buffer_size = 0
    
    for (controls, 
         episode_lengths,
         observations,
         actions,
         memory_states) in zip(
            processes_controls, 
            processes_episode_lengths,
            processes_observations,
            processes_actions,
            processes_memory_states):
        
        for ei in range(controls[UE_SHARED_MEMORY_EXPERIENCE_EPISODE_NUM]):
            ep_len = episode_lengths[ei] - trim_episode_end - trim_episode_start
            if ep_len > 0:
                buffer_size += ep_len
        
    buffer = {
        'obs':          [np.zeros([buffer_size, obs_num], dtype=np.float32) for obs_num in replay_buffer.observation_nums],
        'obs_next':     [np.zeros([buffer_size, obs_num], dtype=np.float32) for obs_num in replay_buffer.observation_nums],
        'act':          [np.zeros([buffer_size, act_num], dtype=np.float32) for act_num in replay_buffer.action_nums],
        'mem':          [np.zeros([buffer_size, mem_num], dtype=np.float32) for mem_num in replay_buffer.memory_state_nums],
        'mem_next':     [np.zeros([buffer_size, mem_num], dtype=np.float32) for mem_num in replay_buffer.memory_state_nums],
        'rew':          [np.zeros([buffer_size, rew_num], dtype=np.float32) for rew_num in replay_buffer.reward_nums],
        'terminated':   np.zeros([buffer_size], dtype=bool),
        'truncated':    np.zeros([buffer_size], dtype=bool),
        'starts':       replay_buffer.episode_starts[1].copy(),
        'lengths':      replay_buffer.episode_lengths[1].copy(),
    }
    
    # Fill Buffer
    
    buffer_offset = 0
    
    for (controls, 
         episode_starts, 
         episode_lengths,
         episode_completion_modes,
         episode_final_observations,
         episode_final_memory_states,
         observations,
         actions,
         memory_states,
         rewards) in zip(
            processes_controls,
            processes_episode_starts,
            processes_episode_lengths,
            processes_episode_completion_modes,
            processes_episode_final_observations,
            processes_episode_final_memory_states,
            processes_observations,
            processes_actions,
            processes_memory_states,
            processes_rewards):
        
        episode_num = controls[UE_SHARED_MEMORY_EXPERIENCE_EPISODE_NUM]
        step_num = controls[UE_SHARED_MEMORY_EXPERIENCE_STEP_NUM]
        assert episode_num > 0 and step_num > 0

        for ei in range(episode_num):
            
            ep_start = episode_starts[ei] + trim_episode_start
            ep_end = episode_starts[ei] + episode_lengths[ei] - trim_episode_end
            ep_len = episode_lengths[ei] - trim_episode_end - trim_episode_start
            
            if ep_len > 0:
            
                obs_list = [observation[ep_start:ep_end] for observation in observations]
                act_list = [action[ep_start:ep_end] for action in actions]
                mem_list = [memory_state[ep_start:ep_end] for memory_state in memory_states]
                rew_list = [reward[ep_start:ep_end] for reward in rewards]
                
                if UE_LEARNING_DEVELOPMENT:
                    for obs in obs_list:
                        assert np.all(np.isfinite(obs))
                    for act in act_list:
                        assert np.all(np.isfinite(act))
                    for mem in mem_list:
                        assert np.all(np.isfinite(mem))
                    for rew in rew_list:
                        assert np.all(np.isfinite(rew))
                
                for index, rew in enumerate(rew_list):
                    avg_rewards[index] += rew.mean()
                    avg_reward_sums[index] += rew.sum()

                avg_episode_length += float(ep_len)
                total_episode_num += 1
                
                obs_nexts = [obs_next_buffer[:ep_len] for obs_next_buffer in replay_buffer.observation_nexts]
                for index, obs_next in enumerate(obs_nexts):
                    obs_next[:-1] = obs_list[index][1:]
                    obs_next[-1] = episode_final_observations[index][ei]

                mem_nexts = [mem_next_buffer[:ep_len] for mem_next_buffer in replay_buffer.memory_state_nexts]
                for index, mem_next in enumerate(mem_nexts):
                    mem_next[:-1] = mem_list[index][1:]
                    mem_next[-1] = episode_final_memory_states[index][ei]

                terminated = replay_buffer.terminated_buffer[:ep_len]
                terminated[:-1] = False
                terminated[-1] = (episode_completion_modes[ei] == UE_COMPLETION_TERMINATED)

                truncated = replay_buffer.truncated_buffer[:ep_len]
                truncated[:-1] = False
                truncated[-1] = (episode_completion_modes[ei] == UE_COMPLETION_TRUNCATED)

                for index, obs in enumerate(obs_list):
                    buffer['obs'][index][buffer_offset:buffer_offset+ep_len] = obs
                    buffer['obs_next'][index][buffer_offset:buffer_offset+ep_len] = obs_nexts[index]

                for index, act in enumerate(act_list):
                    buffer['act'][index][buffer_offset:buffer_offset+ep_len] = act

                for index, mem in enumerate(mem_list):
                    buffer['mem'][index][buffer_offset:buffer_offset+ep_len] = mem
                    buffer['mem_next'][index][buffer_offset:buffer_offset+ep_len] = mem_nexts[index]
                
                for index, rew, in enumerate(rew_list):
                    buffer['rew'][index][buffer_offset:buffer_offset+ep_len] = rew
                
                buffer['terminated'][buffer_offset:buffer_offset+ep_len] = terminated
                buffer['truncated'][buffer_offset:buffer_offset+ep_len] = truncated
                
                buffer_offset += ep_len

        controls[UE_SHARED_MEMORY_NETWORK_ID] = -1
        controls[UE_SHARED_MEMORY_EXPERIENCE_SIGNAL] = 0
    
    assert buffer_offset == buffer_size
    
    stats = {
        'experience/avg_reward':  [0.0 if total_episode_num == 0 else avg_reward / total_episode_num for avg_reward in avg_rewards],
        'experience/avg_reward_sum':  [0.0 if total_episode_num == 0 else avg_reward_sum / total_episode_num for avg_reward_sum in avg_reward_sums],
        'experience/avg_episode_length':  0.0 if total_episode_num == 0 else avg_episode_length / total_episode_num,
    }
    
    return UE_RESPONSE_SUCCESS, buffer, stats
    
    
def shared_memory_receive_experience_behavior_cloning(
    controls,
    replay_buffer_id,
    replay_buffer):

    # Wait until experience is ready
    
    while not controls[UE_SHARED_MEMORY_EXPERIENCE_SIGNAL]:
    
        if controls[UE_SHARED_MEMORY_STOP_SIGNAL]:
            controls[UE_SHARED_MEMORY_STOP_SIGNAL] = 0
            return UE_RESPONSE_STOPPED, None, None
    
        time.sleep(0.001)
    
    # Check buffer ids match
    if controls[UE_SHARED_MEMORY_REPLAY_BUFFER_ID] != replay_buffer_id:
        print('Invalid replay buffer id, expected %d received %d' % (replay_buffer_id, controls[UE_SHARED_MEMORY_REPLAY_BUFFER_ID]))
        return UE_RESPONSE_UNEXPECTED, None, None

    # Copy experience
    
    episode_num = controls[UE_SHARED_MEMORY_EXPERIENCE_EPISODE_NUM]
    step_num = controls[UE_SHARED_MEMORY_EXPERIENCE_STEP_NUM]
    assert episode_num > 0 and step_num > 0

    episode_starts = replay_buffer.episode_starts[1][:episode_num].reshape([episode_num]).copy()
    episode_lengths = replay_buffer.episode_lengths[1][:episode_num].reshape([episode_num]).copy()
    observations = replay_buffer.observations[0][1][:step_num].reshape([step_num, replay_buffer.observation_nums[0]]).copy()
    actions = replay_buffer.actions[0][1][:step_num].reshape([step_num, replay_buffer.action_nums[0]]).copy()

    if UE_LEARNING_DEVELOPMENT:
        assert np.all(np.isfinite(episode_starts))
        assert np.all(np.isfinite(episode_lengths))
        assert np.all(np.isfinite(observations))
        assert np.all(np.isfinite(actions))

    controls[UE_SHARED_MEMORY_EXPERIENCE_SIGNAL] = 0

    buffer = {
        'obs':          observations,
        'act':          actions,
        'starts':       episode_starts,
        'lengths':      episode_lengths,
    }
    
    return (
        UE_RESPONSE_SUCCESS, 
        buffer,
        None)
   

def shared_memory_send_complete(controls):
    controls[UE_SHARED_MEMORY_COMPLETE_SIGNAL] = 1
    return UE_RESPONSE_SUCCESS
    
    
def shared_memory_send_complete_multiprocess(processes_controls):

    for controls in processes_controls:
        controls[UE_SHARED_MEMORY_COMPLETE_SIGNAL] = 1
    
    return UE_RESPONSE_SUCCESS

def shared_memory_has_stop(controls):
    return controls[UE_SHARED_MEMORY_STOP_SIGNAL]

def shared_memory_receive_stop(controls):

    while not controls[UE_SHARED_MEMORY_STOP_SIGNAL]:
        time.sleep(0.001)

    controls[UE_SHARED_MEMORY_STOP_SIGNAL] = 0
    
    return UE_RESPONSE_SUCCESS

def shared_memory_send_ping(controls):
    controls[UE_SHARED_MEMORY_PING_SIGNAL] = 1
    return UE_RESPONSE_SUCCESS

def shared_memory_send_ping_multiprocess(processes_controls):
    
    for controls in processes_controls:
        controls[UE_SHARED_MEMORY_PING_SIGNAL] = 1
        
    return UE_RESPONSE_SUCCESS


class SharedMemoryReplayBuffer:

    def __init__(self, process_num, config, shared_memory_config):

        self.process_num = process_num
        self.max_episode_num = int(config['MaxEpisodeNum'])
        self.max_step_num = int(config['MaxStepNum'])

        self.has_completions = bool(config['HasCompletions'])
        self.has_final_observations = bool(config['HasFinalObservations'])
        self.has_final_memory_states = bool(config['HasFinalMemoryStates'])
        self.is_reinforcement_learning = self.has_completions # This is a little janky

        self.episode_starts = shared_memory_map_array(shared_memory_config['EpisodeStartsGuid'], [process_num, self.max_episode_num], np.int32)
        self.episode_lengths = shared_memory_map_array(shared_memory_config['EpisodeLengthsGuid'], [process_num, self.max_episode_num], np.int32)

        if self.has_completions:
            self.episode_completion_modes = shared_memory_map_array(shared_memory_config['EpisodeCompletionModesGuid'], [process_num, self.max_episode_num], np.uint8)

        self.observation_nums = []
        self.episode_final_observations = []
        self.observations = []
        self.observation_nexts = []
        for index, observation_config in enumerate(config['Observations']):
            observation_id = int(observation_config['Id'])
            observation_name = observation_config['Name']
            observation_schema_id = int(observation_config['SchemaId'])

            observation_num = int(observation_config['VectorDimensionNum'])
            self.observation_nums.append(observation_num)

            if self.has_final_observations:
                self.episode_final_observations.append(
                    shared_memory_map_array(
                        shared_memory_config['EpisodeFinalObservationsGuids'][index],
                        [process_num, self.max_episode_num, observation_num],
                        np.float32))
            self.observations.append(
                shared_memory_map_array(
                    shared_memory_config['ObservationsGuids'][index],
                    [process_num, self.max_step_num, observation_num],
                    np.float32))

            if self.is_reinforcement_learning:
                self.observation_nexts.append(np.zeros([self.max_step_num, observation_num], dtype=np.float32))

        self.action_nums = []
        self.actions = []
        for index, action_config in enumerate(config['Actions']):
            action_id = int(action_config['Id'])
            action_name = action_config['Name']
            action_schema_id = int(action_config['SchemaId'])
            
            action_num = int(action_config['VectorDimensionNum'])
            self.action_nums.append(action_num)

            self.actions.append(
                shared_memory_map_array(
                    shared_memory_config['ActionsGuids'][index],
                    [process_num, self.max_step_num, action_num],
                    np.float32))

        self.memory_state_nums = []
        self.episode_final_memory_states = []
        self.memory_states = []
        self.memory_state_nexts = []
        for index, memory_state_config in enumerate(config['MemoryStates']):
            memory_state_id = int(memory_state_config['Id'])
            memory_state_name = memory_state_config['Name']
            
            memory_state_num = int(memory_state_config['VectorDimensionNum'])
            self.memory_state_nums.append(memory_state_num)

            if self.has_final_memory_states:
                self.episode_final_memory_states.append(shared_memory_map_array(
                    shared_memory_config['EpisodeFinalMemoryStatesGuids'][index],
                    [process_num, self.max_episode_num, memory_state_num],
                    np.float32))
            self.memory_states.append(
                shared_memory_map_array(
                    shared_memory_config['MemoryStatesGuids'][index],
                    [process_num, self.max_step_num, memory_state_num],
                    np.float32))
            self.memory_state_nexts.append(np.zeros([self.max_step_num, memory_state_num], dtype=np.float32))

        self.reward_nums = []
        self.rewards = []
        for index, reward_config in enumerate(config['Rewards']):
            reward_id = int(reward_config['Id'])
            reward_name = reward_config['Name']

            reward_num = int(reward_config['VectorDimensionNum'])
            self.reward_nums.append(reward_num)

            self.rewards.append(
                shared_memory_map_array(
                    shared_memory_config['RewardsGuids'][index],
                    [process_num, self.max_step_num, reward_num],
                    np.float32))

        if self.is_reinforcement_learning:
            self.terminated_buffer = np.zeros([self.max_step_num], dtype=bool)
            self.truncated_buffer = np.zeros([self.max_step_num], dtype=bool)


class SharedMemoryCommunicator:

    def __init__(self, controls_guid, process_num, config_file):

        self.name = 'sharedmemory'

        controls_handle, controls_array = shared_memory_map_array(controls_guid, [process_num, UE_SHARED_MEMORY_CONTROL_NUM], np.int32)
        shared_memory_wait_for_config(controls_array)

        with open(config_file) as f:
            self.config = json.load(f, object_pairs_hook=OrderedDict)

        self.control = (controls_handle, controls_array)
        self.process_num = process_num

        # Setup Neural Networks

        self.network_shared_mem_by_id = dict()
        self.network_info_by_id = dict()

        for network_config in self.config['Networks']:
            network_id = int(network_config['Id'])
            network_name = network_config['Name']
            network_max_bytes = int(network_config['MaxByteNum'])

            network_guid_config = self.config['SharedMemory']['NetworkGuids'][network_id]
            network_guid = network_guid_config['Guid']

            self.network_shared_mem_by_id[network_id] = shared_memory_map_array(network_guid, [network_max_bytes], np.uint8)
            self.network_info_by_id[network_id] = (network_name, network_max_bytes)

        # Setup Replay Buffers

        self.replay_buffers_by_id = dict()

        for index, replay_buffer_config in enumerate(self.config['ReplayBuffers']):
            shared_memory_config = self.config['SharedMemory']['ReplayBuffers'][index]

            replay_buffer_id = int(replay_buffer_config['Id'])
            self.replay_buffers_by_id[replay_buffer_id] = SharedMemoryReplayBuffer(process_num, replay_buffer_config, shared_memory_config)

    def send_network(self, network_id, network):
        return shared_memory_send_network_multiprocess(
            [self.control[1][pi] for pi in range(self.process_num)],
            self.network_shared_mem_by_id[network_id][1],
            network_id,
            network)
        
    def receive_network(self, network_id, network):
        return shared_memory_receive_network_multiprocess(
            [self.control[1][pi] for pi in range(self.process_num)],
            self.network_shared_mem_by_id[network_id][1],
            network_id,
            network)

    def receive_experience(self, replay_buffer_id, trim_episode_start, trim_episode_end):
        replay_buffer = self.replay_buffers_by_id[replay_buffer_id]

        if replay_buffer.is_reinforcement_learning:
            return shared_memory_receive_experience_multiprocess(
                self.control,
                replay_buffer_id,
                replay_buffer,
                trim_episode_start,
                trim_episode_end)
        else:
            return shared_memory_receive_experience_behavior_cloning(
                self.control[1][0],
                replay_buffer_id,
                replay_buffer)

    def send_complete(self):
        return shared_memory_send_complete_multiprocess([self.control[1][pi] for pi in range(self.process_num)])
    
    def send_ping(self):
        return shared_memory_send_ping_multiprocess([self.control[1][pi] for pi in range(self.process_num)])
    
    def has_stop(self):
        return shared_memory_has_stop(self.control[1][0])
    
    def receive_stop(self):
        return shared_memory_receive_stop(self.control[1][0])


# Network Trainers

UE_SOCKET_SIGNAL_INVALID            = 0
UE_SOCKET_SIGNAL_SEND_CONFIG        = 1
UE_SOCKET_SIGNAL_SEND_EXPERIENCE    = 2
UE_SOCKET_SIGNAL_RECEIVE_NETWORK    = 3
UE_SOCKET_SIGNAL_SEND_NETWORK       = 4
UE_SOCKET_SIGNAL_RECEIVE_COMPLETE   = 5
UE_SOCKET_SIGNAL_SEND_STOP          = 6
UE_SOCKET_SIGNAL_RECEIVE_PING       = 7

def socket_receive_all(sock, byte_num):
    data = b''
    while len(data) < byte_num:
        data += sock.recv(byte_num - len(data))
    return data
    
def socket_send_all(sock, data):
    sent = 0
    while sent < len(data):
        sent += sock.send(data[sent:])
    
def socket_send_network(socket, network_id, network, buffer, network_signal):
    save_snapshot(buffer, network)
    socket_send_all(socket, b'%c' % network_signal)
    socket_send_all(socket, network_id.to_bytes(4, 'little'))
    socket_send_all(socket, buffer.tobytes())
    return UE_RESPONSE_SUCCESS

def socket_receive_network(socket, network_id, network, buffer, network_signal):
    signal = ord(socket_receive_all(socket, 1))
    
    if signal != network_signal:
        return UE_RESPONSE_UNEXPECTED
    
    id = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')

    if id != network_id:
        return UE_RESPONSE_UNEXPECTED

    buffer[:] = np.frombuffer(socket_receive_all(socket, len(buffer)), dtype=np.uint8)
    
    load_snapshot(buffer, network)
    
    return UE_RESPONSE_SUCCESS

def socket_send_complete(socket):
    socket_send_all(socket, b'%c' % UE_SOCKET_SIGNAL_RECEIVE_COMPLETE)
    return UE_RESPONSE_SUCCESS

def socket_send_ping(socket):
    socket_send_all(socket, b'%c' % UE_SOCKET_SIGNAL_RECEIVE_PING)
    return UE_RESPONSE_SUCCESS

def socket_receive_experience_reinforcement(
    socket, 
    replay_buffer_id,
    replay_buffer,
    trim_episode_start, 
    trim_episode_end):
    
    signal = ord(socket_receive_all(socket, 1))
    
    if signal == UE_SOCKET_SIGNAL_SEND_STOP:
        return UE_RESPONSE_STOPPED, None, None
        
    if signal != UE_SOCKET_SIGNAL_SEND_EXPERIENCE:
        return UE_RESPONSE_UNEXPECTED, None, None
    
    id = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')

    if id != replay_buffer_id:
        return UE_RESPONSE_UNEXPECTED, None, None

    episode_num = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')
    step_num = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')
    assert episode_num > 0 and step_num > 0

    episode_starts = np.frombuffer(socket_receive_all(socket, episode_num * np.dtype(np.int32).itemsize), dtype=np.int32).reshape([episode_num])
    episode_lengths = np.frombuffer(socket_receive_all(socket, episode_num * np.dtype(np.int32).itemsize), dtype=np.int32).reshape([episode_num])

    episode_completion_modes = np.frombuffer(socket_receive_all(socket, episode_num * np.dtype(np.uint8).itemsize), dtype=np.uint8).reshape([episode_num])

    episode_final_observations = []
    for observation_num in replay_buffer.observation_nums:
        episode_final_observations.append(np.frombuffer(socket_receive_all(socket, episode_num * observation_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([episode_num, observation_num]))

    episode_final_memory_states = []
    for memory_state_num in replay_buffer.memory_state_nums:
        episode_final_memory_states.append(np.frombuffer(socket_receive_all(socket, episode_num * memory_state_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([episode_num, memory_state_num]))

    observations = []
    for observation_num in replay_buffer.observation_nums:
        observations.append(np.frombuffer(socket_receive_all(socket, step_num * observation_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, observation_num]))
    
    actions = []
    for action_num in replay_buffer.action_nums:
        actions.append(np.frombuffer(socket_receive_all(socket, step_num * action_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, action_num]))
    
    memory_states = []
    for memory_state_num in replay_buffer.memory_state_nums:
        memory_states.append(np.frombuffer(socket_receive_all(socket, step_num * memory_state_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, memory_state_num]))
    
    rewards = []
    for reward_num in replay_buffer.reward_nums:
        rewards.append(np.frombuffer(socket_receive_all(socket, step_num * reward_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, reward_num]))

    avg_rewards = [0.0 for _ in rewards]
    avg_reward_sums = [0.0 for _ in rewards]
    avg_episode_length = 0.0
    total_episode_num = 0

    # Compute Buffer Size
    
    buffer_size = 0
    
    for ei in range(episode_num):
        ep_len = episode_lengths[ei] - trim_episode_end - trim_episode_start
        if ep_len > 0:
            buffer_size += ep_len
    
    
    buffer = {
        'obs':          [np.zeros([buffer_size, obs_num], dtype=np.float32) for obs_num in replay_buffer.observation_nums],
        'obs_next':     [np.zeros([buffer_size, obs_num], dtype=np.float32) for obs_num in replay_buffer.observation_nums],
        'act':          [np.zeros([buffer_size, act_num], dtype=np.float32) for act_num in replay_buffer.action_nums],
        'mem':          [np.zeros([buffer_size, mem_num], dtype=np.float32) for mem_num in replay_buffer.memory_state_nums],
        'mem_next':     [np.zeros([buffer_size, mem_num], dtype=np.float32) for mem_num in replay_buffer.memory_state_nums],
        'rew':          [np.zeros([buffer_size, rew_num], dtype=np.float32) for rew_num in replay_buffer.reward_nums],
        'terminated':   np.zeros([buffer_size], dtype=bool),
        'truncated':    np.zeros([buffer_size], dtype=bool),
    }
    
    # Fill Buffer
    
    buffer_offset = 0

    for ei in range(episode_num):

        ep_start = episode_starts[ei] + trim_episode_start
        ep_end = episode_starts[ei] + episode_lengths[ei] - trim_episode_end
        ep_len = episode_lengths[ei] - trim_episode_end - trim_episode_start
        
        if ep_len > 0:

            obs_list = [observation[ep_start:ep_end] for observation in observations]
            act_list = [action[ep_start:ep_end] for action in actions]
            mem_list = [memory_state[ep_start:ep_end] for memory_state in memory_states]
            rew_list = [reward[ep_start:ep_end] for reward in rewards]
            
            if UE_LEARNING_DEVELOPMENT:
                for obs in obs_list:
                    assert np.all(np.isfinite(obs))
                for act in act_list:
                    assert np.all(np.isfinite(act))
                for mem in mem_list:
                    assert np.all(np.isfinite(mem))
                for rew in rew_list:
                    assert np.all(np.isfinite(rew))
            
            for index, rew in enumerate(rew_list):
                avg_rewards[index] += rew.mean()
                avg_reward_sums[index] += rew.sum()

            avg_episode_length += float(ep_len)
            total_episode_num += 1
            
            obs_nexts = [obs_next_buffer[:ep_len] for obs_next_buffer in replay_buffer.observation_next_buffers]
            for index, obs_next in enumerate(obs_nexts):
                obs_next[:-1] = obs_list[index][1:]
                obs_next[-1] = episode_final_observations[index][ei]

            mem_nexts = [mem_next_buffer[:ep_len] for mem_next_buffer in replay_buffer.mem_next_buffers]
            for index, mem_next in enumerate(mem_nexts):
                mem_next[:-1] = mem_list[index][1:]
                mem_next[-1] = episode_final_memory_states[index][ei]

            terminated = replay_buffer.terminated_buffer[:ep_len]
            terminated[:-1] = False
            terminated[-1] = (episode_completion_modes[ei] == UE_COMPLETION_TERMINATED)

            truncated = replay_buffer.truncated_buffer[:ep_len]
            truncated[:-1] = False
            truncated[-1] = (episode_completion_modes[ei] == UE_COMPLETION_TRUNCATED)

            for index, obs in enumerate(obs_list):
                buffer['obs'][index][buffer_offset:buffer_offset+ep_len] = obs
                buffer['obs_next'][index][buffer_offset:buffer_offset+ep_len] = obs_nexts[index]

            for index, act in enumerate(act_list):
                buffer['act'][index][buffer_offset:buffer_offset+ep_len] = act

            for index, mem in enumerate(mem_list):
                buffer['mem'][index][buffer_offset:buffer_offset+ep_len] = mem
                buffer['mem_next'][index][buffer_offset:buffer_offset+ep_len] = mem_nexts[index]
            
            for index, rew, in enumerate(rew_list):
                buffer['rew'][index][buffer_offset:buffer_offset+ep_len] = rew
            
            buffer['terminated'][buffer_offset:buffer_offset+ep_len] = terminated
            buffer['truncated'][buffer_offset:buffer_offset+ep_len] = truncated
            
            buffer_offset += ep_len
    
    assert buffer_offset == buffer_size
    
    stats = {
        'experience/avg_reward':  [0.0 if total_episode_num == 0 else avg_reward / total_episode_num for avg_reward in avg_rewards],
        'experience/avg_reward_sum':  [0.0 if total_episode_num == 0 else avg_reward_sum / total_episode_num for avg_reward_sum in avg_reward_sums],
        'experience/avg_episode_length':  0.0 if total_episode_num == 0 else avg_episode_length / total_episode_num,
    }
    
    return UE_RESPONSE_SUCCESS, buffer, stats


def socket_receive_experience_behavior_cloning(
    socket,
    replay_buffer_id,
    replay_buffer):
    
    observation_num = replay_buffer.observation_nums[0]
    action_num = replay_buffer.action_nums[0]

    signal = ord(socket_receive_all(socket, 1))
    
    if signal != UE_SOCKET_SIGNAL_SEND_EXPERIENCE:
        return UE_RESPONSE_UNEXPECTED, None, None
    
    id = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')

    if id != replay_buffer_id:
        return UE_RESPONSE_UNEXPECTED, None, None

    episode_num = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')
    step_num = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')
    assert episode_num > 0 and step_num > 0

    episode_starts = np.frombuffer(socket_receive_all(socket, episode_num * np.dtype(np.int32).itemsize), dtype=np.int32).reshape([episode_num]).copy()
    episode_lengths = np.frombuffer(socket_receive_all(socket, episode_num * np.dtype(np.int32).itemsize), dtype=np.int32).reshape([episode_num]).copy()
    observations = np.frombuffer(socket_receive_all(socket, step_num * observation_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, observation_num]).copy()
    actions = np.frombuffer(socket_receive_all(socket, step_num * action_num * np.dtype(np.float32).itemsize), dtype=np.float32).reshape([step_num, action_num]).copy()

    if UE_LEARNING_DEVELOPMENT:
        assert np.all(np.isfinite(episode_starts))
        assert np.all(np.isfinite(episode_lengths))
        assert np.all(np.isfinite(observations))
        assert np.all(np.isfinite(actions))

    buffer = {
        'obs':          observations,
        'act':          actions,
        'starts':       episode_starts,
        'lengths':      episode_lengths,
    }

    return (
        UE_RESPONSE_SUCCESS, 
        buffer,
        None)


def socket_receive_config(socket):

    signal = ord(socket_receive_all(socket, 1))
    if signal != UE_SOCKET_SIGNAL_SEND_CONFIG:
        return UE_RESPONSE_UNEXPECTED, None

    config_length = int.from_bytes(socket_receive_all(socket, 4), byteorder='little')
    
    config = socket_receive_all(socket, config_length)

    return UE_RESPONSE_SUCCESS, config
    
    
def socket_has_stop(socket):
    r, _, _ = select.select([socket],[],[],0)
    return socket in r

def socket_receive_stop(socket):

    signal = ord(socket_receive_all(socket, 1))
    if signal != UE_SOCKET_SIGNAL_SEND_STOP:
        return UE_RESPONSE_UNEXPECTED, None

    return UE_RESPONSE_SUCCESS


class SocketReplayBuffer:

    def __init__(self, config):

        self.max_episode_num = int(config['MaxEpisodeNum'])
        self.max_step_num = int(config['MaxStepNum'])

        self.has_completions = bool(config['HasCompletions'])
        self.has_final_observations = bool(config['HasFinalObservations'])
        self.has_final_memory_states = bool(config['HasFinalMemoryStates'])
        self.is_reinforcement_learning = self.has_completions # This is a little janky

        self.observation_nums = []
        self.observation_next_buffers = []
        for observation_config in config['Observations']:
            observation_id = int(observation_config['Id'])
            observation_name = observation_config['Name']
            observation_schema_id = int(observation_config['SchemaId'])

            observation_num = int(observation_config['VectorDimensionNum'])
            self.observation_nums.append(observation_num)

            if self.is_reinforcement_learning:
                self.observation_next_buffers.append(np.zeros([self.max_step_num, observation_num], dtype=np.float32))

        self.action_nums = []
        for action_config in config['Actions']:
            action_id = int(action_config['Id'])
            action_name = action_config['Name']
            action_schema_id = int(action_config['SchemaId'])
            action_num = int(action_config['VectorDimensionNum'])

            self.action_nums.append(action_num)

        self.memory_state_nums = []
        self.mem_next_buffers = []
        for memory_state_config in config['MemoryStates']:
            memory_state_id = int(memory_state_config['Id'])
            memory_state_name = memory_state_config['Name']
            memory_state_num = int(memory_state_config['VectorDimensionNum'])

            self.memory_state_nums.append(memory_state_num)
            self.mem_next_buffers.append(np.zeros([self.max_step_num, memory_state_num], dtype=np.float32))

        self.reward_nums = []
        for reward_config in config['Rewards']:
            reward_id = int(reward_config['Id'])
            reward_name = reward_config['Name']
            reward_num = int(reward_config['VectorDimensionNum'])

            self.reward_nums.append(reward_num)

        self.terminated_buffer = np.zeros([self.max_step_num], dtype=bool)
        self.truncated_buffer = np.zeros([self.max_step_num], dtype=bool)


class SocketCommunicator:

    def __init__(self, socket, config):

        self.name = 'socket'
        self.socket = socket

        # Setup Neural Networks

        self.network_buffers_by_id = dict()
        self.network_info_by_id = dict()

        for network_config in config['Networks']:
            network_id = int(network_config['Id'])
            network_name = network_config['Name']
            network_max_bytes = int(network_config['MaxByteNum'])

            self.network_buffers_by_id[network_id] = np.empty([network_max_bytes], dtype=np.uint8)
            self.network_info_by_id[network_id] = (network_name, network_max_bytes)
        
        # Setup Replay Buffers

        self.replay_buffers_by_id = dict()

        for replay_buffer_config in config['ReplayBuffers']:
            replay_buffer_id = int(replay_buffer_config['Id'])
            self.replay_buffers_by_id[replay_buffer_id] = SocketReplayBuffer(replay_buffer_config)
        
    # NOTE: Send and Receive are swapped below in the signal constant because they refer to
    # what is happening on the C++ side.
        
    def send_network(self, network_id, network):
        return socket_send_network(
            self.socket,
            network_id,
            network,
            self.network_buffers_by_id[network_id],
            UE_SOCKET_SIGNAL_RECEIVE_NETWORK)
        
    def receive_network(self, network_id, network):
        return socket_receive_network(
            self.socket,
            network_id,
            network,
            self.network_buffers_by_id[network_id],
            UE_SOCKET_SIGNAL_SEND_NETWORK)
    
    def receive_experience(self, replay_buffer_id, trim_episode_start, trim_episode_end):
        replay_buffer = self.replay_buffers_by_id[replay_buffer_id]
        if replay_buffer.is_reinforcement_learning:
            return socket_receive_experience_reinforcement(
                self.socket,
                replay_buffer_id,
                self.replay_buffers_by_id[replay_buffer_id],
                trim_episode_start, 
                trim_episode_end)
        else:
            return socket_receive_experience_behavior_cloning(
                self.socket,
                replay_buffer_id,
                replay_buffer)
        
    def send_complete(self):
        return socket_send_complete(self.socket)

    def send_ping(self):
        return socket_send_ping(self.socket)
    
    def has_stop(self):
        return socket_has_stop(self.socket)
    
    def receive_stop(self):
        return socket_receive_stop(self.socket)


# Functions for operating on action schemas

def _mask_to_indices_exclusive(m, device):
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        indices[np.where(m[i] == 1)[0][0]].append(i)
    return [torch.as_tensor(i, dtype=torch.long, device=device) for i in indices]


def _mask_to_indices_inclusive(m, device):
    indices = [[] for i in range(m.shape[1])]
    for i in range(m.shape[0]):
        for j in np.where(m[i] == 1)[0]:
            indices[j].append(i)
    return [torch.as_tensor(i, dtype=torch.long, device=device) for i in indices]
    
    
def schema_act_num(act_schema):
    
    act_type = act_schema['Type']
    
    if act_type == 'Null':
        return 0
    
    if act_type == 'Continuous':
        return act_schema['VectorSize']
    
    elif act_type == 'DiscreteExclusive':
        return act_schema['VectorSize']
    
    elif act_type == 'DiscreteInclusive':
        return act_schema['VectorSize']
        
    elif act_type == 'And':
        return sum([schema_act_num(element) for element in act_schema['Elements'].values()])
    
    elif act_type in ('OrExclusive', 'OrInclusive'):
        return len(act_schema['Elements']) + sum([schema_act_num(element) for element in act_schema['Elements'].values()])
    
    elif act_type == 'Array':
        return act_schema['Num'] * schema_act_num(act_schema['Element'])
        
    elif act_type == 'Encoding':
        return schema_act_num(act_schema['Element'])
        
    else:
        raise Exception('Not Implemented')


half_plus_half_log_2pi = 0.5 + 0.5 * np.log(2 * np.pi)
log_sqrt_2pi = np.log(np.sqrt(2 * np.pi))

def independent_normal_entropy(log_std):
    return (half_plus_half_log_2pi + log_std)

def multinoulli_entropy(logits):
    log_probs = torch.log_softmax(logits, dim=-1)
    probs = torch.exp(torch.clip(log_probs, None, 10))
    return -(probs * log_probs)
    
def bernoulli_entropy(logits):
    neg_prob = torch.sigmoid(-logits)
    pos_prob = torch.sigmoid(+logits)
    neg_log_prob = -F.softplus(+logits)
    pos_log_prob = -F.softplus(-logits)
    return -(neg_log_prob * neg_prob + pos_log_prob * pos_prob)

def schema_entropy(act_schema, act_dist):
    
    act_type = act_schema['Type']
    assert act_schema['DistributionSize'] == act_dist.shape[1]
    
    if act_type == 'Null':
        return torch.zeros([len(act_dist)], device=act_dist.device)
    
    if act_type == 'Continuous':
        _, act_log_std = act_dist[:,:act_dist.shape[1]//2], act_dist[:,act_dist.shape[1]//2:]
        assert act_schema['VectorSize'] == act_log_std.shape[1]
        return independent_normal_entropy(act_log_std).sum(dim=-1)
    
    elif act_type == 'DiscreteExclusive':
        return multinoulli_entropy(act_dist).sum(dim=-1)
    
    elif act_type == 'DiscreteInclusive':
        return bernoulli_entropy(act_dist).sum(dim=-1)
        
    elif act_type == 'And':
    
        total = torch.zeros_like(act_dist[:,0])
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += schema_entropy(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset == act_schema['DistributionSize']
        
        return total
    
    elif act_type == 'OrExclusive':
    
        elem_num = len(act_schema['Elements'])
        elem_entropy = multinoulli_entropy(act_dist[:,-elem_num:])
    
        total = torch.zeros_like(act_dist[:,0])
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += elem_entropy[:,ei] + schema_entropy(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset + elem_num == act_schema['DistributionSize']
        
        return total
        
    elif act_type == 'OrInclusive':
    
        elem_num = len(act_schema['Elements'])
        elem_entropy = bernoulli_entropy(act_dist[:,-elem_num:])
    
        total = torch.zeros_like(act_dist[:,0])
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += elem_entropy[:,ei] + schema_entropy(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset + elem_num == act_schema['DistributionSize']
            
        return total
    
    elif act_type == 'Array':
    
        batchsize = len(act_dist)
        act_dist_reshape = act_dist.reshape([batchsize * act_schema['Num'], -1])
        entropy = schema_entropy(act_schema['Element'], act_dist_reshape)
        return entropy.reshape([batchsize, act_schema['Num']]).sum(dim=-1)
        
    elif act_type == 'Encoding':
        return schema_entropy(act_schema['Element'], act_dist)
        
    else:
        raise Exception('Not Implemented')


def independent_normal_log_prob(mean, log_std, value):
    std = torch.exp(torch.clip(log_std, None, 10.0))
    return (-((value - mean) ** 2) / (2 * (std ** 2)) - log_std - log_sqrt_2pi)

def multinoulli_log_prob(logits, value):
    log_probs = torch.log_softmax(logits, dim=-1)
    return (log_probs * value)
    
def bernoulli_log_prob(logits, value):
    neg_log_prob = -F.softplus(+logits)
    pos_log_prob = -F.softplus(-logits)
    return (neg_log_prob * (1 - value) + pos_log_prob * value)

def schema_log_prob(act_schema, act_dist, act_sample):
    
    act_type = act_schema['Type']
    assert act_schema['DistributionSize'] == act_dist.shape[1]
    assert act_schema['VectorSize'] == act_sample.shape[1]

    if act_type == 'Null':
        return torch.zeros([len(act_dist)], device=act_dist.device)
    
    if act_type == 'Continuous':
        act_mean, act_log_std = act_dist[:,:act_dist.shape[1]//2], act_dist[:,act_dist.shape[1]//2:]
        assert act_schema['DistributionSize'] == 2 * act_schema['VectorSize']
        assert act_schema['VectorSize'] == act_mean.shape[1]
        assert act_schema['VectorSize'] == act_log_std.shape[1]
        return independent_normal_log_prob(act_mean, act_log_std, act_sample).sum(dim=-1)
    
    elif act_type == 'DiscreteExclusive':
        return multinoulli_log_prob(act_dist, act_sample).sum(dim=-1)
    
    elif act_type == 'DiscreteInclusive':
        return bernoulli_log_prob(act_dist, act_sample).sum(dim=-1)
        
    elif act_type == 'And':
    
        total = torch.zeros_like(act_dist[:,0])
        dist_offset = 0
        smpl_offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            dist_size = element['DistributionSize']
            smpl_size = element['VectorSize']
           
            total += schema_log_prob(
                element, 
                act_dist[:,dist_offset:dist_offset+dist_size],
                act_sample[:,smpl_offset:smpl_offset+smpl_size])
                
            dist_offset += dist_size
            smpl_offset += smpl_size
        assert dist_offset == act_schema['DistributionSize']
        assert smpl_offset == act_schema['VectorSize']

        return total
    
    elif act_type == 'OrExclusive':
        
        dist_sizes = [element['DistributionSize'] for element in act_schema['Elements'].values()]
        smpl_sizes = [element['VectorSize'] for element in act_schema['Elements'].values()]
        dist_offsets = np.hstack([0, np.cumsum(dist_sizes[:-1])])
        
        elem_num = len(act_schema['Elements'])
        elem_logp = multinoulli_log_prob(act_dist[:,-elem_num:], act_sample[:,-elem_num:])
        
        with torch.no_grad():
            elem_mask_np = act_sample[:,-elem_num:].cpu().numpy()
            elem_indices = _mask_to_indices_exclusive(elem_mask_np, device=act_sample.device)
            
        total = elem_logp.sum(dim=-1)

        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            if len(elem_indices[ei]) == 0: continue
            
            total[elem_indices[ei]] += schema_log_prob(
                element, 
                act_dist[elem_indices[ei],dist_offsets[ei]:dist_offsets[ei]+dist_sizes[ei]],
                act_sample[elem_indices[ei],:smpl_sizes[ei]])
        
        assert np.sum(dist_sizes) + elem_num == act_schema['DistributionSize']
        assert np.max(smpl_sizes) + elem_num == act_schema['VectorSize']
        
        return total
    
    elif act_type == 'OrInclusive':

        dist_sizes = [element['DistributionSize'] for element in act_schema['Elements'].values()]
        smpl_sizes = [element['VectorSize'] for element in act_schema['Elements'].values()]
        dist_offsets = np.hstack([0, np.cumsum(dist_sizes[:-1])])
        smpl_offsets = np.hstack([0, np.cumsum(smpl_sizes[:-1])])

        elem_num = len(act_schema['Elements'])
        elem_logp = bernoulli_log_prob(act_dist[:,-elem_num:], act_sample[:,-elem_num:])
        
        with torch.no_grad():
            elem_mask_np = act_sample[:,-elem_num:].cpu().numpy()
            elem_indices = _mask_to_indices_inclusive(elem_mask_np, device=act_sample.device)
        
        total = elem_logp.sum(dim=-1)

        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            if len(elem_indices[ei]) == 0: continue

            total[elem_indices[ei]] += schema_log_prob(
                element, 
                act_dist[elem_indices[ei],dist_offsets[ei]:dist_offsets[ei]+dist_sizes[ei]],
                act_sample[elem_indices[ei],smpl_offsets[ei]:smpl_offsets[ei]+smpl_sizes[ei]])

        assert np.sum(dist_sizes) + elem_num == act_schema['DistributionSize']
        assert np.sum(smpl_sizes) + elem_num == act_schema['VectorSize']

        return total
        
    elif act_type == 'Array':
        
        batchsize = len(act_dist)
        act_dist_reshape = act_dist.reshape([batchsize * act_schema['Num'], -1])
        act_sample_reshape = act_sample.reshape([batchsize * act_schema['Num'], -1])
        logp = schema_log_prob(act_schema['Element'], act_dist_reshape, act_sample_reshape)
        return logp.reshape([batchsize, act_schema['Num']]).sum(dim=-1)

    elif act_type == 'Encoding':
        return schema_log_prob(act_schema['Element'], act_dist, act_sample)
        
    else:
        raise Exception('Not Implemented')
    
    
def schema_regularization(act_schema, act_dist):
    
    act_type = act_schema['Type']
    assert act_schema['DistributionSize'] == act_dist.shape[1]
    
    if act_type == 'Null':
        return torch.zeros([len(act_dist)], device=act_dist.device)
    
    if act_type == 'Continuous':
        act_mean, act_log_std = act_dist[:,:act_dist.shape[1]//2], act_dist[:,act_dist.shape[1]//2:]
        return abs(act_mean).sum(dim=-1) + abs(act_log_std).sum(dim=-1)
    
    elif act_type == 'DiscreteExclusive':
        return abs(act_dist).sum(dim=-1)
    
    elif act_type == 'DiscreteInclusive':
        return abs(act_dist).sum(dim=-1)
        
    elif act_type == 'And':
        
        total = torch.zeros_like(act_dist[:,0])
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += schema_regularization(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset == act_schema['DistributionSize']

        return total
    
    elif act_type == 'OrExclusive':
        
        elem_num = len(act_schema['Elements'])

        total = abs(act_dist[:,-elem_num:]).sum(dim=-1)
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += schema_regularization(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset + len(act_schema['Elements']) == act_schema['DistributionSize']

        return total
    
    elif act_type == 'OrInclusive':
        
        elem_num = len(act_schema['Elements'])

        total = abs(act_dist[:,-elem_num:]).sum(dim=-1)
        offset = 0
        for ei, element in enumerate(act_schema['Elements'].values()):
            assert ei == element['Index']
            size = element['DistributionSize']
            total += schema_regularization(element, act_dist[:,offset:offset+size])
            offset += size
        assert offset + len(act_schema['Elements']) == act_schema['DistributionSize']

        return total
    
    elif act_type == 'Array':
    
        batchsize = len(act_dist)
        act_dist_reshape = act_dist.reshape([batchsize * act_schema['Num'], -1])
        reg = schema_regularization(act_schema['Element'], act_dist_reshape)
        return reg.reshape([batchsize, act_schema['Num']]).sum(dim=-1)
    
    elif act_type == 'Encoding':
        return schema_regularization(act_schema['Element'], act_dist)
    
    else:
        raise Exception('Not Implemented')
