'''
	Implement a workflow for a node in Python

    This is setting preset deformRig values per body type
'''

import unreal

class provider:

    _provider_handles = {}

    def __call__(self, in_pin):
        pin_name = 'DeformRig_Value'
        if not hasattr(in_pin, 'get_node'):
            return [unreal.RigVMUserWorkflow()]
        if in_pin.get_linked_source_pins():
            return [unreal.RigVMUserWorkflow()]            
        if in_pin.get_name() != pin_name:
            return [unreal.RigVMUserWorkflow()]
        
        workflows = []
        
        #M_LRG
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.PIN_CONTEXT
        workflow.title = 'Set to M_LRG'
        workflow.tooltip = 'Set default values to M_LRG preset values'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow_M_LRG)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()
        workflows.append(workflow)

        # M_MED
        # create a new workflow
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.PIN_CONTEXT
        workflow.title = 'Set to M_MED'
        workflow.tooltip = 'Set default values to M_MED preset values'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow_M_MED)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()
        workflows.append(workflow)

        # F_LRG
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.PIN_CONTEXT
        workflow.title = 'Set to F_LRG'
        workflow.tooltip = 'Set default values to F_LRG preset values'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow_F_LRG)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()
        workflows.append(workflow)

        # F_MED
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.PIN_CONTEXT
        workflow.title = 'Set to F_MED'
        workflow.tooltip = 'Set default values to F_MED preset values'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow_F_MED)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()
        workflows.append(workflow)

        # return a list of workflows for this provider
        return workflows
    
    def get_preset_value(self, body_type=None):    
        M_LRG_dict = {'upperarm_up':'Translation=(X=0.0,Y=0.0,Z=8.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)', 
                    'upperarm_fwd':'Translation=(X=0.0,Y=10.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'upperarm_fwd_bk':'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'upperarm_bk':'Translation=(X=0.0,Y=-4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'elbow_in':'Translation=(X=0.0,Y=9.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'elbow_out':'Translation=(X=0.0,Y=-2.0,Z=0.0),Scale3D=(X=0.750000,Y=1.0,Z=1.0)',
                    'wrist_up':'Translation=(X=0.0,Y=9.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                    'wrist_dn':'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                    'groin':'Translation=(X=-3.0,Y=12.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'glute':'Translation=(X=-7.0,Y=0.0,Z=0.0),Scale3D=(X=1.0,Y=1.50,Z=1.0)',
                    'knee_in':'Translation=(X=0.0,Y=-12.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'knee_out':'Translation=(X=0.0,Y=3.0,Z=0.0),Scale3D=(X=1.50,Y=1.0,Z=1.50)',
                    'bicep':'Translation=(X=-3.0,Y=4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'lat':'Translation=(X=0.0,Y=0.0,Z=10.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'pec':'Translation=(X=0.0,Y=2.0,Z=3.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)'
                    }
                    
        M_MED_dict = {'upperarm_up':'Translation=(X=0.0,Y=0.0,Z=8.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)', 
                    'upperarm_fwd':'Translation=(X=0.0,Y=10.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'upperarm_fwd_bk':'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'upperarm_bk': 'Translation=(X=0.0,Y=-4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'elbow_in':'Translation=(X=0.0,Y=9.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'elbow_out':'Translation=(X=0.0,Y=-2.0,Z=0.0),Scale3D=(X=0.750000,Y=1.0,Z=1.0)',
                    'wrist_up':'Translation=(X=0.0,Y=5.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                    'wrist_dn':'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                    'groin':'Translation=(X=-3.0,Y=12.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'glute':'Translation=(X=-7.0,Y=0.0,Z=0.0),Scale3D=(X=1.0,Y=1.50,Z=1.0)',
                    'knee_in':'Translation=(X=0.0,Y=-12.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                    'knee_out':'Translation=(X=0.0,Y=3.0,Z=0.0),Scale3D=(X=1.50,Y=1.0,Z=1.50)',
                    'bicep':'Translation=(X=-3.0,Y=4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'lat':'Translation=(X=0.0,Y=0.0,Z=10.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                    'pec':'Translation=(X=0.0,Y=2.0,Z=3.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)'
                    }

        F_LRG_dict = {'upperarm_up': 'Translation=(X=0.0,Y=0.0,Z=8.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'upperarm_fwd': 'Translation=(X=0.0,Y=10.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'upperarm_fwd_bk': 'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'upperarm_bk': 'Translation=(X=0.0,Y=-7.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'elbow_in': 'Translation=(X=0.0,Y=9.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'elbow_out': 'Translation=(X=0.0,Y=-2.5,Z=0.0),Scale3D=(X=0.6,Y=1.0,Z=1.0)',
                      'wrist_up': 'Translation=(X=0.0,Y=7.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'wrist_dn': 'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'groin': 'Translation=(X=0.0,Y=8.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'glute': 'Translation=(X=-5.0,Y=0.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'knee_in': 'Translation=(X=0.0,Y=-14.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'knee_out': 'Translation=(X=0.0,Y=2.5,Z=0.0),Scale3D=(X=1.25,Y=1.0,Z=1.25)',
                      'bicep': 'Translation=(X=-3.0,Y=4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'lat': 'Translation=(X=0.0,Y=0.0,Z=10.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'pec': 'Translation=(X=0.0,Y=2.0,Z=3.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)'
                      }

        F_MED_dict = {'upperarm_up': 'Translation=(X=0.0,Y=0.0,Z=8.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'upperarm_fwd': 'Translation=(X=0.0,Y=10.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'upperarm_fwd_bk': 'Translation=(X=0.0,Y=-5.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'upperarm_bk': 'Translation=(X=0.0,Y=-4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'elbow_in': 'Translation=(X=0.0,Y=8.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'elbow_out': 'Translation=(X=0.0,Y=-2.0,Z=0.0),Scale3D=(X=0.750000,Y=1.0,Z=1.0)',
                      'wrist_up': 'Translation=(X=0.0,Y=5.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'wrist_dn': 'Translation=(X=0.0,Y=-6.0,Z=0.0),Scale3D=(X=0.70,Y=1.0,Z=1.0)',
                      'groin': 'Translation=(X=-3.0,Y=10.0,Z=0.0),Scale3D=(X=0.60,Y=1.0,Z=1.0)',
                      'glute': 'Translation=(X=-3.0,Y=0.0,Z=0.0),Scale3D=(X=1.0,Y=1.25,Z=1.0)',
                      'knee_in': 'Translation=(X=0.0,Y=-10.0,Z=0.0),Scale3D=(X=0.50,Y=1.0,Z=1.0)',
                      'knee_out': 'Translation=(X=0.0,Y=2.0,Z=0.0),Scale3D=(X=1.2,Y=1.0,Z=1.2)',
                      'bicep': 'Translation=(X=-3.0,Y=4.0,Z=0.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'lat': 'Translation=(X=0.0,Y=0.0,Z=10.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)',
                      'pec': 'Translation=(X=0.0,Y=2.0,Z=3.0),Scale3D=(X=1.0,Y=1.0,Z=1.0)'
                      }
        
        val_dict = {}
        if body_type == 'M_LRG':
            val_dict = M_LRG_dict
        elif body_type == 'M_MED':
            val_dict = M_MED_dict
        elif body_type == 'F_LRG':
            val_dict = F_LRG_dict
        elif body_type == 'F_MED':
            val_dict = F_MED_dict

        vals  = '(upperarm_up_18_EC3E7F2D49F194DF7852879628AE361A=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {upperarm_up}),' \
                'upperarm_fwd_fr_19_3597600644E81495DCE68085E1E3B213=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {upperarm_fwd}),' \
                'upperarm_fwd_bk_20_9973AAD84EE0A1E349BC9E85067D4A3C=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {upperarm_fwd_bk}),' \
                'upperarm_bk_22_BC7A7EED432F9FC35E3F30830B7BC0DC=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {upperarm_bk}),' \
                'elbow_in_24_0AEEC41843B3C28036C085B09C47E077=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {elbow_in}),' \
                'elbow_out_26_55EF075C4B65D90B9ADAA1A63501595D=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {elbow_out}),' \
                'wrist_up_31_08F632C14E91E4A25C9A8F932EFF67BC=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {wrist_up}),' \
                'wrist_dn_32_6C820F1E4318C77C62117BBAC5B05EF9=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {wrist_dn}),' \
                'groin_35_09CC0B0944ACCE29D708CBB2226A3154=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {groin}),' \
                'glute_36_2AD3C9934F85E8E19BE5E590C043D687=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {glute}),' \
                'knee_in_41_BBCE8D81404E07072413DABDDE26B7E0=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {knee_in}),' \
                'knee_out_42_2ED1ADDF4524F3B88B2C9EA071E7A5E2=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {knee_out}),' \
                'bicep_44_8EB45FB4416336E094E596B4A8895EDE=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {bicep}),' \
                'lat_45_050D0CAE411BD5B9AEB4B5A60FA2335B=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {lat}),' \
                'pec_51_58FB7CE84C6A464BE1FBE3AFDD2522D1=(Rotation=(X=0.0,Y=-0.0,Z=0.0,W=1.0), {pec}))'''.format(**val_dict)

        return vals

    def perform_user_workflow_M_LRG(self, in_options, in_controller):
        #get node name
        node = in_options.subject
        node_name = node.get_node().get_name()
        pin_name = 'DeformRig_Value'

        print('setting "{}" to "M_LRG" preset.'.format(pin_name))
        in_controller.set_pin_default_value('{}.{}'.format(node_name, pin_name), self.get_preset_value(body_type='M_LRG'), True)
        
        return True

    def perform_user_workflow_M_MED(self, in_options, in_controller):
        # get node name
        node = in_options.subject
        node_name = node.get_node().get_name()
        pin_name = 'DeformRig_Value'

        print('setting "{}" to "M_MED" preset.'.format(pin_name))
        in_controller.set_pin_default_value('{}.{}'.format(node_name, pin_name), self.get_preset_value(body_type='M_MED'), True)

        return True
    
    def perform_user_workflow_F_LRG(self, in_options, in_controller):
        # get node name
        node = in_options.subject
        node_name = node.get_node().get_name()
        pin_name = 'DeformRig_Value'

        print('setting "{}" to "F_LRG" preset.'.format(pin_name))
        in_controller.set_pin_default_value('{}.{}'.format(node_name, pin_name), self.get_preset_value(body_type='F_LRG'), True)

        return True
    
    def perform_user_workflow_F_MED(self, in_options, in_controller):
        # get node name
        node = in_options.subject
        node_name = node.get_node().get_name()
        pin_name = 'DeformRig_Value'

        print('setting "{}" to "F_MED" preset.'.format(pin_name))
        in_controller.set_pin_default_value('{}.DeformRig_Value'.format(node_name), self.get_preset_value(body_type='F_MED'), True)

        return True

    @classmethod
    def register(cls):
        print ('registering {}'.format(cls))
        # retrieve the node we want to add a workflow to
        unit_struct = None

        # create an empty callback and bind an instance of this class to it
        provider_callback = unreal.RigVMUserWorkflowProvider()
        provider_callback.bind_callable(cls())

        # remember the registered provider handle so we can unregister later
        handle = unreal.RigVMUserWorkflowRegistry.get().register_provider(unit_struct, provider_callback)

        # we also store the callback on the class 
        # so that it doesn't get garbage collected
        cls._provider_handles[handle] = provider_callback

    @classmethod
    def unregister(cls):
        print('unregistering {}'.format(cls))
        for handle in cls._provider_handles:
            unreal.RigVMUserWorkflowRegistry.get().unregister_provider(handle)
        cls._provider_handles = {}

'''
def _reregister():
    # for use in UE to reload this module for testing
    # define and run this func in the interactive editor scope
    workflow_deformation_rig_preset.provider.unregister()
    reload(workflow_deformation_rig_preset)
    workflow_deformation_rig_preset.provider.register()
'''
    