# Copyright Epic Games, Inc. All Rights Reserved.

import shutil
import flow.cmd
import textwrap
import importlib
from pathlib import Path

#-------------------------------------------------------------------------------
class Help(flow.cmd.Cmd):
    """ Prints information about the available commands """

    def main(self):
        channel = self.get_channel()
        system = channel.get_system()
        cmd_tree = system.get_command_tree()
        root_node = cmd_tree.get_root_node()

        def read_invokeables(node, path=""):
            if node.is_invokeable():
                yield path, node

            for child_name, child_node in node.read_children():
                child_path = (path + " " + child_name) if path else child_name
                yield from read_invokeables(child_node, child_path)

        def read_output():
            for path, node in read_invokeables(root_node):
                if path.startswith("$"):
                    continue

                try:
                    command_class = node.get_command_class()
                    command_class = command_class.get_class_type()
                    yield path, command_class
                except Exception as e:
                    yield path, "LOAD_ERROR: " + str(e)

        right_column = shutil.get_terminal_size()[0]
        right_column -= 5

        output = list(read_output())
        output.sort(key=lambda x: x[0])

        name_len = max(len(x[0]) for x in output) + 4
        lead_dot = "." * (name_len - 2)
        lead_spc = " " * name_len
        desc_width = right_column - name_len
        for name, cmd_class in output:
            dots = lead_dot[len(name):]
            leader = flow.cmd.text.light_cyan(name) + " " + flow.cmd.text.grey(dots)
            if isinstance(cmd_class, str):
                desc = cmd_class
            else:
                desc = cmd_class.get_desc(pretty=desc_width)
                desc = desc.replace("\n", "\n" + lead_spc)
            print(" ", leader, sep="", end="")
            print(desc)
            print()



#-------------------------------------------------------------------------------
class ReadMe(flow.cmd.Cmd):
    """ Opens the README.txt file in your editor of choice or prints it to stdout """
    editor = flow.cmd.Opt(False, "Open in a text editor")

    def main(self):
        for haystack_dir in Path(__file__).parents:
            readme_path = haystack_dir / "README.txt"
            if readme_path.is_file():
                break
        else:
            self.print_error("Unable to find README.txt")
            return False

        if self.args.editor:
            self.edit_file(readme_path)
            return

        with readme_path.open("rt") as file:
            for line in file.readlines():
                line = line.strip()
                if line.startswith("#"):
                    with flow.cmd.text.light_cyan:
                        print(line)
                else:
                    print(line)
