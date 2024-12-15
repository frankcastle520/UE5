# -*- coding: utf-8 -*-
'''
Copyright Epic Games, Inc. All Rights Reserved.
'''

import numpy as np
import torch
from torch import nn

from train_common import UE_LEARNING_DEVELOPMENT, UE_LEARNING_PROFILE, Profile
from train_common import schema_act_num, schema_entropy, schema_log_prob, schema_regularization

def print_array_details(name, arr):
    """ Prints various stats for an array - used for debugging """
    
    print(
        name,
        arr.min(), 
        arr.max(), 
        arr.mean(), 
        arr.std(), 
        np.any(~np.isfinite(arr)),
        np.any(np.isinf(arr)),
        np.any(np.isposinf(arr)),
        np.any(np.isneginf(arr)),
        np.any(np.isnan(arr)),
        arr)

def check_policy_input_output(check_name, policy_input, policy_output, policy_mem):
    """ Checks the inputs and outputs of the policy for nans/infs """
    
    if not UE_LEARNING_DEVELOPMENT: return
    
    with torch.no_grad():
    
        output_mu = policy_output[0].detach().cpu().numpy()
        output_sigma = policy_output[1].detach().cpu().numpy()
        output_mem = policy_mem.detach().cpu().numpy()
        
        if np.any(~np.isfinite(policy_input)):
            print_array_details(check_name+'_input', policy_input)

        if np.any(~np.isfinite(output_mu)):
            print_array_details(check_name+'_output_mu', output_mu)
            
        if np.any(~np.isfinite(output_sigma)):
            print_array_details(check_name+'_output_sigma', output_sigma)

        if np.any(~np.isfinite(output_mem)):
            print_array_details(check_name+'_output_mem', output_mem)

def check_array(check_name, arr):
    """ Checks an array for nans/infs """
    
    if not UE_LEARNING_DEVELOPMENT: return
    
    with torch.no_grad():
        arr_numpy = arr.detach().cpu().numpy()

        if np.any(~np.isfinite(arr_numpy)):
            print_array_details(check_name, arr_numpy)

        
def check_network(check_name, network):
    """ Checks a network for nans/infs """
    
    if not UE_LEARNING_DEVELOPMENT: return
    
    with torch.no_grad():
        for name, param in network.named_parameters():
            param_numpy = param.detach().cpu().numpy()
            if np.any(~np.isfinite(param_numpy)):
                print_array_details(check_name + '_' + name, param_numpy)

def check_network_gradients(check_name, network):
    """ Checks a network's gradients for nans/infs """
    
    if not UE_LEARNING_DEVELOPMENT: return
    
    with torch.no_grad():
        for name, param in network.named_parameters():
            param_grad_numpy = param.grad.detach().cpu().numpy()
            if np.any(~np.isfinite(param_grad_numpy)):
                print_array_details(check_name + '_' + name + '_grad', param_grad_numpy)

def check_scalar(check_name, value):
    """ Checks a scalar for nans/infs """
    
    if not UE_LEARNING_DEVELOPMENT: return
    
    with torch.no_grad():
        value_item = value.item()
        if not np.isfinite(value_item):
            print(check_name, value_item)
    
    
class PPOTrainer:
    """ This class is a basic implementation of PPO which supports recurrent policies and some 
    of the more common heuristics such as clipping and normalizing advantages. It is designed
    to give fairly robust and stable results across a range of tasks, reward designs, etc.
    
    Keyword arguments:
        observation_schema          -- The JSON object describing the observation structure
        action_schema               -- The JSON object describing the action structure
        policy                      -- The policy pytorch Module
        critic                      -- The critic pytorch Module
        encoder                     -- The encoder pytorch Module
        decoder                     -- The decoder pytorch Module
        optim_policy                -- The policy pytorch Optimizer
        optim_critic                -- The critic pytorch Optimizer
        discount_factor             -- The discount factor
        gae_lambda                  -- The lambda parameter for Generalized Advantage Estimation
        eps_clip                    -- The PPO epsilon clipping parameter. Should be between 0 and 1
        advantage_normalization     -- If to normalize the advantages. This makes training more robust to varying reward scales.
        advantage_min               -- The minimum advanage to clip to. Set this to 0.0 to only allow positive advantages.
        advantage_max               -- The maximum advanage to clip to. Reduce this to clip large advantages that could be outliers.
        use_grad_norm_max_clipping  -- If to use gradient norm max clipping. Set this as True if training is unstable or leave as False if unused.
        grad_norm_max               -- The maximum gradient norm to clip updates to.
        action_surr_weight          -- The weight used for the surrogate objective loss used by PPO.
        action_reg_weight           -- The regularization weight to use for actions. Make this larger to encourage the policy to make small, sparse actions.
        action_ent_weight           -- The entropy weight to use for actions. Make this larger to encourage the policy to expore more.
        return_reg_weight           -- The return regularization weight. Make this larger to encourage the critic not to over-estimate returns.
    """
    
    def __init__(
        self,
        observation_schema,
        action_schema,
        policy: torch.nn.Module,
        critic: torch.nn.Module,
        encoder: torch.nn.Module,
        decoder: torch.nn.Module,
        optim_policy: torch.optim.Optimizer,
        optim_critic: torch.optim.Optimizer,
        discount_factor = 0.99,
        gae_lambda = 0.95,
        eps_clip = 0.2,
        advantage_normalization = True,
        advantage_min = 0.0,
        advantage_max = 10.0,
        use_grad_norm_max_clipping = False,
        grad_norm_max = 0.5,
        action_surr_weight = 1.0,
        action_reg_weight = 0.001,
        action_ent_weight = 0.0,
        return_reg_weight = 0.0001):

        self.observation_schema = observation_schema
        self.action_schema = action_schema
        self.act_enc_num = self.action_schema['EncodedSize']
        self.act_num = schema_act_num(self.action_schema)
        
        self.policy = policy
        self.critic = critic
        self.encoder = encoder
        self.decoder = decoder
        
        self.optim_policy = optim_policy
        self.optim_critic = optim_critic
        
        self.discount_factor = discount_factor
        self.gae_lambda = gae_lambda
        self.eps_clip_log_min = np.log(1.0 - eps_clip)
        self.eps_clip_log_max = np.log(1.0 + eps_clip)
        self.adv_norm = advantage_normalization
        self.adv_min = advantage_min
        self.adv_max = advantage_max
        self.use_grad_norm_max_clipping = use_grad_norm_max_clipping
        self.grad_norm_max = grad_norm_max
        self.action_surr_weight = action_surr_weight
        self.action_reg_weight = action_reg_weight
        self.action_ent_weight = action_ent_weight
        self.return_reg_weight = return_reg_weight
    
    
    def compute_advantages_and_returns(self, obs, obs_next, mem, mem_next, rew, terminated, truncated, warn_about_advantages=True):
        
        # This is a slightly inefficient way to compute gae since it involves evaluating the
        # critic twice on the data. As so, this should probably be refactored later however 
        # right now it represents a very small part of the total training cost.
        
        with torch.no_grad():
            val = self.critic(torch.cat([self.encoder(obs), mem], dim=-1))[...,0].cpu().numpy()
            val_next = self.critic(torch.cat([self.encoder(obs_next), mem_next], dim=-1))[...,0].cpu().numpy()

        discount = (~(terminated | truncated)) * self.discount_factor * self.gae_lambda
        delta = rew + (~terminated) * val_next * self.discount_factor - val
        
        adv = np.zeros_like(rew)
        for i in reversed(range(len(rew) - 1)):
            adv[i] = delta[i] + discount[i] * adv[i+1]
        
        returns = adv + val
        returns_mean = returns.mean()
        returns = torch.as_tensor(returns, device=self.policy.device)
        
        # Here we normalize and clips advantages. Normalizing advantages is a kind of
        # "safe" way of making the policy learning behaviour independant of the overall
        # magnitude of the rewards/returns. Clipping negative advantages tends to also
        # be much more stable as making bad actions less likely can often simply push 
        # the policy into a different mode of bad actions - which is unstable.
        
        if warn_about_advantages and adv.std() < 1e-8:
            print('Warning: Advantages standard deviation very small: %f!' % adv.std())

        if self.adv_norm:
            adv = adv / np.maximum(adv.std(), 1e-8)
        
        if self.adv_min is not None or self.adv_max is not None:
           adv = adv.clip(self.adv_min, self.adv_max)
        
        if warn_about_advantages and np.all(adv == 0.0):
            print('Warning: All advantages were zero - no better than average actions found! Critic may be under-trained.')
        
        adv = torch.as_tensor(adv, device=self.policy.device)
    
        return adv, returns, returns_mean
    
        
    def compute_old_logp(self, obs, mem, act):

        with torch.no_grad():
            act_enc_mem = self.policy(torch.cat([self.encoder(obs), mem], dim=-1))
            act_dist = self.decoder(act_enc_mem[:,:self.act_enc_num])
            logp = schema_log_prob(self.action_schema, act_dist, act)
        
        return logp
        
        
    def train_critic(self, obs, mem, returns):
        
        # Reset Gradients
        
        self.optim_critic.zero_grad()
        
        # Predict Returns
        
        pred_returns = self.critic(torch.cat([self.encoder(obs), mem], dim=-1))[...,0]

        # Compute Losses
        
        # Note: here we use the l1 loss rather than the l2 loss for the critic.
        #
        # Although this means the critic is no longer really learning the _expected_ 
        # discounted return any more this loss is far more stable to large variations 
        # in the magnitude of the return which is important for us since we don't know 
        # what kind of reward scales the users will have picked or their discount factor.
        #
        # I prefer this solution to normalizing returns or rewards since the scale of these
        # can drift a lot during training as the agent improves or encounters new challenges
        # and we still want to preserve the relative magnitudes when comparing these states
        # to avoid the agent getting worse without knowing it.
        
        loss_ret = abs(returns - pred_returns).mean()
        loss_reg = self.return_reg_weight * abs(pred_returns).mean()
        loss_critic = loss_ret + loss_reg
        
        # Update Weights
        
        loss_critic.backward()
        
        if self.use_grad_norm_max_clipping:
            nn.utils.clip_grad_norm_(self.encoder.parameters(), max_norm=self.grad_norm_max)
            nn.utils.clip_grad_norm_(self.critic.parameters(), max_norm=self.grad_norm_max)
            
        self.optim_critic.step()
        
        # Compute gradient norm

        with torch.no_grad():
            encoder_grad_norm = torch.zeros([1]) if list(self.encoder.parameters()) == [] else torch.norm(torch.stack([torch.norm(p.grad) for p in self.encoder.parameters() if p.grad is not None]))
            critic_grad_norm = torch.norm(torch.stack([torch.norm(p.grad) for p in self.critic.parameters() if p.grad is not None]))
            
        return (
            encoder_grad_norm.item(),
            critic_grad_norm.item(),
            loss_ret.item(),
            loss_reg.item(),
            loss_critic.item())
        
    
    def train_policy(self, mask, obs, act, mem, adv, logp, policy_window):
        
        # Reset Gradients
        
        self.optim_policy.zero_grad()

        # Compute Losses
        
        loss_surr = 0.0
        loss_ent = 0.0
        loss_reg = 0.0
        
        policy_mem = mem[:,0]
        
        for i in range(0, policy_window):
            
            act_enc_policy_mem = self.policy(torch.cat([self.encoder(obs[:,i]), policy_mem], dim=-1))
            act_enc, policy_mem = act_enc_policy_mem[:,:self.act_enc_num],  act_enc_policy_mem[:,self.act_enc_num:]
            act_dist = self.decoder(act_enc)
            
            # Note: here we keep the ratio and surrogate losses in the log probability space.
            # This is different to most PPO implementations which put them through an 
            # exponent to put it into the actual probability space and optimize for that.
            #
            # However I've found that staying in the log space tends to be a little more stable for 
            # training and to me makes more sense from an optimization standpoint where typically it
            # is better to optimize for log likelihoods rather than actual likelihoods/probabilities.
            #
            # Also note that to do this we need to convert the eps clip parameters into the log space.
            # This I've also found to be more stable since the raw ratio before clipping can be very 
            # large after it is put through an exponent and can cause nans and infs. Finally, it makes 
            # the code a little more simple, neat, and performant since it avoids the exp.
            
            ratio = schema_log_prob(self.action_schema, act_dist, act[:,i]) - logp[:,i]
            
            surr = torch.min(ratio, ratio.clamp(self.eps_clip_log_min, self.eps_clip_log_max))
            
            loss_surr += self.action_surr_weight * (mask[:,i] * adv[:,i] * -(surr / self.act_num)).mean() / policy_window
            loss_ent += self.action_ent_weight * (mask[:,i] * -(schema_entropy(self.action_schema, act_dist) / self.act_num)).mean() / policy_window
            loss_reg += self.action_reg_weight * (mask[:,i] * (schema_regularization(self.action_schema, act_dist) / self.act_num)).mean() / policy_window

        loss_policy = loss_surr + loss_ent + loss_reg
        
        # Update Weights
        
        loss_policy.backward()
    
        if self.use_grad_norm_max_clipping:
            nn.utils.clip_grad_norm_(self.policy.parameters(), max_norm=self.grad_norm_max)
            nn.utils.clip_grad_norm_(self.encoder.parameters(), max_norm=self.grad_norm_max)
            nn.utils.clip_grad_norm_(self.decoder.parameters(), max_norm=self.grad_norm_max)
        
        self.optim_policy.step()
        
        # Compute gradient norm

        with torch.no_grad():
            encoder_grad_norm = torch.zeros([1]) if list(self.encoder.parameters()) == [] else torch.norm(torch.stack([torch.norm(p.grad) for p in self.encoder.parameters() if p.grad is not None]))
            decoder_grad_norm = torch.zeros([1]) if list(self.decoder.parameters()) == [] else torch.norm(torch.stack([torch.norm(p.grad) for p in self.decoder.parameters() if p.grad is not None]))
            policy_grad_norm = torch.norm(torch.stack([torch.norm(p.grad) for p in self.policy.parameters() if p.grad is not None]))
            
        return (
            encoder_grad_norm.item(),
            decoder_grad_norm.item(),
            policy_grad_norm.item(),
            loss_surr.item(),
            loss_ent.item(),
            loss_reg.item(),
            loss_policy.item())
    
    
    def extract_from_buffer(self, buffer):
    
        obs = torch.as_tensor(buffer['obs'], device=self.policy.device)
        obs_next = torch.as_tensor(buffer['obs_next'], device=self.policy.device)
        act = torch.as_tensor(buffer['act'], device=self.policy.device)
        mem = torch.as_tensor(buffer['mem'], device=self.policy.device)
        mem_next = torch.as_tensor(buffer['mem_next'], device=self.policy.device)
        rew = buffer['rew']
        terminated = buffer['terminated']
        truncated = buffer['truncated']
            
        if np.all(rew == 0.0): print('Warning: All rewards were zero!')
        if np.all(terminated | truncated): print('Warning: All episodes were zero length!')
    
        return (obs, obs_next, act, mem, mem_next, rew, terminated, truncated)
    
    
    def warmup_critic(
        self,
        buffer,
        critic_batch_size,
        recompute_returns_iterations=10,
        train_iterations=25,
        update_func=None):
        
        (obs,
         obs_next,
         act,
         mem,
         mem_next,
         rew,
         terminated,
         truncated) = self.extract_from_buffer(buffer)
        
        grads_encoder = []
        grads_critic = []
        critic_ret_losses = []
        critic_reg_losses = []         
        critic_losses = []
        
        for _ in range(recompute_returns_iterations):
            
            # Recompute Returns
            
            _, returns, _ = self.compute_advantages_and_returns(
                obs, 
                obs_next, 
                mem, 
                mem_next, 
                rew, 
                terminated, 
                truncated,
                warn_about_advantages=False)
        
            # Train Critic
            
            for _ in range(train_iterations):
            
                indices = torch.randint(0, len(obs), size=[critic_batch_size])
                
                grad_encoder, grad_critic, loss_ret, loss_reg, loss_critic = self.train_critic(
                    obs[indices],
                    mem[indices],
                    returns[indices])
                
                grads_encoder.append(grad_encoder)
                grads_critic.append(grad_critic)
                critic_ret_losses.append(loss_ret)
                critic_reg_losses.append(loss_reg)
                critic_losses.append(loss_critic)
                
                if update_func is not None:
                    update_func()
                
        return {
            "grads/encoder": grads_encoder,
            "grads/critic": grads_critic,
            "loss/critic_ret": critic_ret_losses,
            "loss/critic_reg": critic_reg_losses,
            "loss/critic": critic_losses,
        }

    
    def train(
        self, 
        buffer, 
        policy_batch_size, 
        critic_batch_size, 
        iterations, 
        policy_window,
        update_func=None):
        
        with Profile('PPO load tensors', UE_LEARNING_PROFILE):
            
            (obs,
             obs_next,
             act,
             mem,
             mem_next,
             rew,
             terminated,
             truncated) = self.extract_from_buffer(buffer)
        
        with Profile('PPO gae', UE_LEARNING_PROFILE):
        
            adv, returns, returns_mean = self.compute_advantages_and_returns(
                obs, obs_next, 
                mem, mem_next, 
                rew, 
                terminated, truncated)
        
        with Profile('PPO log prob', UE_LEARNING_PROFILE):
        
            logp = self.compute_old_logp(obs, mem, act)
        
        with Profile('PPO create batches', UE_LEARNING_PROFILE):
        
            assert policy_window >= 1
            
            # Here we construct windows of a fixed size over the trajectory data
            # for training the policy in a recurrent way. We also make a binary
            # mask for masking out the losses when the generation process runs 
            # over the boundary between two episodes.
            
            window_indices = []
            window_masks = []
            
            for i in range(len(obs) - policy_window):
                
                window_indices.append(i + np.arange(policy_window))

                done_indices = (terminated | truncated)[i:i+policy_window].nonzero()[0]
                
                if len(done_indices) > 0:
                    window_masks.append(np.arange(policy_window) <= done_indices.min())
                else:
                    window_masks.append(np.ones([policy_window], dtype=bool))
                
            window_indices = np.array(window_indices)
            window_masks = np.array(window_masks)
            
            window_indices = torch.as_tensor(window_indices, device=self.critic.device, dtype=torch.long)
            window_masks = torch.as_tensor(window_masks, device=self.critic.device, dtype=torch.bool)
        

        with Profile('PPO train', UE_LEARNING_PROFILE):
        
            grads_encoder = []
            grads_decoder = []
            grads_policy = []
            grads_critic = []

            critic_ret_losses = []
            critic_reg_losses = []         
            critic_losses = []        
            
            policy_surr_losses = []
            policy_ent_losses = []
            policy_reg_losses = []
            policy_losses = []
        
            for _ in range(iterations):
                
                # Train Critic

                critic_batch_indices = torch.randint(0, len(obs), size=[critic_batch_size])
                
                critic_grad_encoder, grad_critic, loss_ret, loss_reg, loss_critic = self.train_critic(
                    obs[critic_batch_indices],
                    mem[critic_batch_indices],
                    returns[critic_batch_indices])
                
                critic_ret_losses.append(loss_ret)
                critic_reg_losses.append(loss_reg)
                critic_losses.append(loss_critic)
                
                # Train Policy
                
                policy_batch = torch.randint(0, len(window_indices), size=[policy_batch_size])
                policy_batch_indices = window_indices[policy_batch]
                policy_batch_masks = window_masks[policy_batch]

                policy_grad_encoder, grad_decoder, grad_policy, loss_surr, loss_ent, loss_reg, loss_policy = self.train_policy(
                    policy_batch_masks, 
                    obs[policy_batch_indices], 
                    act[policy_batch_indices], 
                    mem[policy_batch_indices], 
                    adv[policy_batch_indices], 
                    logp[policy_batch_indices],
                    policy_window)
                
                policy_surr_losses.append(loss_surr)
                policy_ent_losses.append(loss_ent)
                policy_reg_losses.append(loss_reg)
                policy_losses.append(loss_policy)
                
                # Append Grads

                grads_encoder.append((critic_grad_encoder + policy_grad_encoder) / 2)
                grads_decoder.append(grad_decoder)
                grads_policy.append(grad_policy)
                grads_critic.append(grad_critic)

                if update_func is not None:
                    update_func()

        return {
            "grads/encoder": grads_encoder,
            "grads/decoder": grads_decoder,
            "grads/policy": grads_policy,
            "grads/critic": grads_critic,
            "loss/critic_ret": critic_ret_losses,
            "loss/critic_reg": critic_reg_losses,
            "loss/critic": critic_losses,
            "loss/policy_surr": policy_surr_losses,
            "loss/policy_ent": policy_ent_losses,
            "loss/policy_reg": policy_reg_losses,
            "loss/policy": policy_losses,
            "experience/avg_return": returns_mean,
        }

