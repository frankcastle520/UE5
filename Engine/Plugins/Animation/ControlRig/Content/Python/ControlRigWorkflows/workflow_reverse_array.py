'''
Workflow implementation for Rig Elements Array
Reverse Array in Item Array
'''

import unreal

class provider:

    _provider_handles = {}

    def __call__(self, in_node):
        # create a new workflow
        workflow = unreal.RigVMUserWorkflow()
        workflow.type = unreal.RigVMUserWorkflowType.NODE_CONTEXT 
        workflow.title = 'Reverse Array'
        workflow.tooltip = 'Reverse Item Array'
        workflow.on_perform_workflow.bind_callable(self.perform_user_workflow)

        # choose the default options class. you can define your own classes
        # if you want to provide options to the user to choose from.
        workflow.options_class = unreal.ControlRigWorkflowOptions.static_class()
        
        # return a list of workflows for this provider
        return [workflow]

    def perform_user_workflow(self, in_options, in_controller):

        node = in_options.subject

        # gets the pin path from the Items Array node
        items_pin = node.find_pin("Items").get_pin_path()
        array_size = node.find_pin("Items").get_array_size() - 1
        item_name = []
        item_type = []

        # appends item name and item type default value 
        # to item_name and item_type while going through Items Array

        for i in range(array_size, -1, -1):
            item_name.append(
                in_controller.get_pin_default_value(
                    node.find_pin("Items." + str(i) + ".Name").get_pin_path()
                    )
                )
            item_type.append(
                in_controller.get_pin_default_value(
                    node.find_pin("Items." + str(i) + ".Type").get_pin_path()
                    )
                )

        # deletes the old pins
        in_controller.clear_array_pin(items_pin)

        # creates new pins with the default values reversed and expands them
        x = 0
        while x <= array_size:
            new_pin = in_controller.add_array_pin(
                array_pin_path = items_pin, default_value = '', 
                setup_undo_redo = True, print_python_command = True
                )
            in_controller.set_pin_default_value(
                pin_path = new_pin + '.Name', default_value = item_name[x], 
                resize_arrays = True
                )
            in_controller.set_pin_default_value(
                pin_path = new_pin + '.Type', default_value = item_type[x], 
                resize_arrays = True
                )
            in_controller.set_pin_expansion(
                pin_path = new_pin, is_expanded = True
                )
            x+=1
        return True

    @classmethod
    def register(cls):
        # retrieve the node we want to add a workflow to
        unit_struct = unreal.RigUnit_ItemArray.static_struct()

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
        for (handle, provider) in cls._provider_handles:
            unreal.RigVMUserWorkflowRegistry.get().unregister_provider(handle)
        cls._provider_handles = {}