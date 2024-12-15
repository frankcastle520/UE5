# Copyright Epic Games, Inc. All Rights Reserved.
#
#
# Helper functions for the Movie Render Graph, each function is a static method
# which helps testing individual functions in isolation. This module is used
# in the MovieGraphEditorExample python file 

import unreal

@staticmethod
def on_queue_finished_callback(executor: unreal.MoviePipelineExecutorBase, success: bool):
    """Is called after the executor has finished rendering all jobs

    Args:
        success (bool): True if all jobs completed successfully, false if a job 
                        encountered an error (such as invalid output directory)
                        or user cancelled a job (by hitting escape)
        executor (unreal.MoviePipelineExecutorBase): The executor that run this queue
    """
    unreal.log("on_queue_finished_callback Render completed. Success: " + str(success))


@staticmethod
def set_global_output_settings_node(job:unreal.MoviePipelineExecutorJob):
    '''
    This example demonstrates how to make a modification to the global Output Settings
    Node to edit the default values. If you are interested in just overriding some exposed
    values, check the "set_user_exposed_variables" which is a more appropiate workflow
    for per-job overrides.

    Note: This is modifiying the actual shared graph asset and dirtying it.
    '''
    # Get the Graph Asset from the Job that we want to search for the Output Settings Node
    graph = job.get_graph_preset()

    # Get the Globals Output Node
    globals_pin_on_output = graph.get_output_node().get_input_pin("Globals")

    # Assume that the Output Settings Node is connected to the Globals Pin
    output_settings_node = globals_pin_on_output.get_connected_nodes()[0]
    
    if not isinstance(output_settings_node, unreal.MovieGraphGlobalOutputSettingNode):
        unreal.log("This example expected that the Global Output Settings node is plugged into the Globals Node")
        return

    output_settings_node.set_editor_property("override_output_resolution", True)
    # Override output resolution
    output_settings_node.set_editor_property("output_resolution", 
                unreal.MovieGraphLibrary.named_resolution_from_profile("720p (HD)"))


@staticmethod
def set_job_parameters(job:unreal.MoviePipelineExecutorJob):
    """This function showcases how job Parameters can be set or modified. By
    using the set_editor_property method, we ensure that the changes mark the Queue 
    as dirty

    Args:
        job (unreal.MoviePipelineExecutorJob): the Pipeline Job to be modified
    """
    job.set_editor_property("sequence", unreal.SoftObjectPath('/Game/Levels/shots/shot0010/shot0010.shot0010'))
    job.set_editor_property("map", unreal.SoftObjectPath('/Game/Levels/Main_LVL.Main_LVL'))

    job.set_editor_property("job_name", "shot0010")
    job.set_editor_property("author", "Automated.User")
    job.set_editor_property("comment", "This comment was created through Python")


@staticmethod
def set_user_exposed_variables(job:unreal.MoviePipelineExecutorJob):
    """Finds the user exposed variable CustomOutputResolution and modifies it

    Args:
        job (unreal.MoviePipelineExecutorJob): the Pipeline Job which we will 
                                        use to find the graph preset to modify
    """
    graph = job.get_graph_preset()
    variables = graph.get_variables()

    if not variables:
        print("No variables are exposed on this graph, expose 'CustomOutputResolution' to test this example")

    for variable in variables:
        if variable.get_member_name() == "CustomOutputRes":

            # Get Variable Assignments on Graphs or subgraphs 
            variable_assignment = job.get_or_create_variable_overrides(graph)

            # Set new Value
            variable_assignment.set_value_serialized_string(variable, 
                unreal.MovieGraphLibrary.named_resolution_from_profile("720p (HD)").export_text())

            # Enable override toggle
            variable_assignment.set_variable_assignment_enable_state(variable, True)


@staticmethod
def duplicate_queue(pipeline_queue:unreal.MoviePipelineQueue):
    """
    Duplicating a queue is desirable in an interactive session, especially when you 
    want to modify a copy of the Queue Asset instead of altering the original one.
    Args:
        queue (unreal.MoviePipelineQueue): The Queue which we want to duplicate
    """
    new_queue = unreal.MoviePipelineQueue()
    new_queue.copy_from(pipeline_queue)
    pipeline_queue = new_queue
    return pipeline_queue 


@staticmethod
def advanced_job_operations(job:unreal.MoviePipelineExecutorJob):
    """
    Wrapper function that runs the following functions on the current job
    - set_job_parameters
    - set_user_exposed_variables
    - set_global_output_settings_node

    Args:
        job (unreal.MoviePipelineJob): The current processed Queue Job
    """
    if not job.get_graph_preset():
        unreal.log("This Job doesn't have a graph type preset, add a graph preset to the job to test this function")
        return

    # Set Job parameters such as Author/Level/LevelSequence
    set_job_parameters(job)

    # Set user exposed variables on the Graph config
    set_user_exposed_variables(job)
    
    # Set attributes on the actual graph's nodes directly, this is like
    # setting the default values
    set_global_output_settings_node(job)



@staticmethod
def traverse_graph_config(graph:unreal.MovieGraphConfig):
    """Demonstrates how we can use depth first search to visit all the nodes starting
    from the "Globals" pin and navigating our way to the left until all nodes are 
    exhausted

    Args:
        graph (unreal.MovieGraphConfig): The graph to operate on
    """
    visited = set()

    def dfs(node, visisted=None):
        visited.add(node.get_name())
        
        # Nodes can have different number of input nodes and names which we need to collect
        if isinstance(node, unreal.MovieGraphSubgraphNode) or isinstance(node, unreal.MovieGraphOutputNode):
            pins = [node.get_input_pin("Globals"), node.get_input_pin("Input")]
        elif isinstance(node, unreal.MovieGraphBranchNode):
            pins = [node.get_input_pin("True"), node.get_input_pin("False")]
        elif isinstance(node, unreal.MovieGraphSelectNode):
            pins = [node.get_input_pin("Default")]

        else:
            pins = [node.get_input_pin("")]

        # Iterate over the found pins
        for pin in pins:
            if pin:
                for neighbour in pin.get_connected_nodes():
                    dfs(neighbour, visited)
