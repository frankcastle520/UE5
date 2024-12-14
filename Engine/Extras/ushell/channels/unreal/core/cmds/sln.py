# Copyright Epic Games, Inc. All Rights Reserved.

import os
import shutil
import unrealcmd
from pathlib import Path

#-------------------------------------------------------------------------------
class _Base(unrealcmd.MultiPlatformCmd):
    def _get_primary_name(self):
        if hasattr(self, "_primary_name"):
            return self._primary_name

        ue_context = self.get_unreal_context()
        engine = ue_context.get_engine()

        name = "UE" + str(engine.get_version_major())
        try:
            ppn_path = engine.get_dir() / "Intermediate/ProjectFiles/PrimaryProjectName.txt"
            if not ppn_path.is_file():
                # Old path, needs to be maintained while projects prior to UE5.1 are supported
                ppn_path = engine.get_dir() / ("Intermediate/ProjectFiles/M" + "asterProjectName.txt")
            with ppn_path.open("rb") as ppn_file:
                name = ppn_file.read().decode().strip()
        except FileNotFoundError:
            self.print_warning("Project files may be ungenerated")

        self._primary_name = name
        return name

    def _get_sln_path(self):
        ue_context = self.get_unreal_context()
        engine = ue_context.get_engine()
        primary_name = self._get_primary_name()
        return engine.get_dir().parent / (primary_name + ".sln")

    def _open_sln(self):
        if os.name != "nt":
            self.print_error("Opening project files is only supported on Windows")
            return False

        self.print_info("Opening project")
        sln_path = self._get_sln_path()
        print("Path:", sln_path)
        if not os.path.isfile(sln_path):
            self.print_error("Project file not found")
            return False

        print("Enumerating VS instances")
        import vs.dte
        for i, instance in enumerate(vs.dte.running()):
            print(f" {i} ", end="")
            if not (instance_path := instance.get_sln_path()):
                print("no-sln")
                continue
            print(instance_path, end="")

            if sln_path.samefile(instance_path):
                if instance.activate():
                    print(" ...activating")
                    return True
            print()

        os.chdir(os.path.dirname(sln_path))

        run_args = ("cmd.exe", "/c", "start", sln_path)
        shell_open = self.get_exec_context().create_runnable(*run_args)
        return shell_open.run()

#-------------------------------------------------------------------------------
class Generate(_Base):
    """ Generates a Visual Studio solution for the active project. To more
    easily distinguish open solutions from one branch to the next, the generated
    .sln file will be suffixed with the current branch name. Use the '--all'
    option to include all the current branch's projects. To generate-then-open
    invoke as '.sln generate open'."""
    open    = unrealcmd.Arg("", "If this argument is 'open' then open after generating")
    ubtargs = unrealcmd.Arg([str], "Additional arguments pass to UnrealBuildTool")
    notag   = unrealcmd.Opt(False, "Do not tag the solution name with a branch identifier")
    all     = unrealcmd.Opt(False, "Include all the branch's projects")

    def complete_open(self, prefix):
        yield "open"

    def get_exec_context(self):
        context = super().get_exec_context()
        if not self.args.notag:
            context.get_env()["UE_NAME_PROJECT_AFTER_FOLDER"] = "1"
        return context

    def main(self):
        self.use_all_platforms()
        ue_context = self.get_unreal_context()

        exec_context = self.get_exec_context()
        args = ("-ProjectFiles", *self.args.ubtargs)
        if not self.args.all:
            if project := ue_context.get_project():
                args = ("-Project=" + str(project.get_path()), *args)

        self.print_info("Generating project files")
        ubt = ue_context.get_engine().get_ubt()
        for cmd, args in ubt.read_actions(*args):
            cmd = exec_context.create_runnable(cmd, *args)
            ret = cmd.run()
            if ret:
                return ret

        if self.args.open == "open":
            return self._open_sln()

#-------------------------------------------------------------------------------
class Open(_Base):
    """ Opens project files in Visual Studio. """

    def get_exec_context(self):
        context = super().get_exec_context()

        # Detect when UE_NAME_PROJECT_AFTER_FOLDER was used to generate the
        # project files and set it to allow tools like UnrealVS to generate
        # the project files consistently.
        primary_parts = self._get_primary_name().split("_", 1)
        if len(primary_parts) > 1:
            ue_context = self.get_unreal_context()
            engine = ue_context.get_engine()
            folder_name = engine.get_dir().parent.parent.name
            if folder_name == primary_parts[1]:
                context.get_env()["UE_NAME_PROJECT_AFTER_FOLDER"] = "1"

        return context

    def main(self):
        self.use_all_platforms()
        return self._open_sln()

#-------------------------------------------------------------------------------
class Open10x(_Base):
    """ Opens a Visual Studio Solution in 10x Editor """

    def main(self):
        if os.name != "nt":
            self.print_error("Opening Visual Studio Solution in 10x Editor is only supported on Windows")
            return False

        self.print_info("Opening solution in 10x Editor")
        sln_path = self._get_sln_path()
        print("Path:", sln_path)
        if not os.path.isfile(sln_path):
            self.print_error("Project file not found")
            return False

        # At the moment there is no env var set for 10x, but Stewart will be adding this for us
        # Assume default install location for now.
        editor_path: str = r"C:\Program Files\PureDevSoftware\10x\10x.exe"
        if not os.path.isfile(sln_path):
            self.print_error("10x.exe not found at default install location")
            return False

        args = (
            sln_path,
        )

        cmd = self.get_exec_context().create_runnable(editor_path, *args)
        result = cmd.launch()
        return True



#-------------------------------------------------------------------------------
class Tiny(_Base):
    """ Quickly generates and opens a tiny solution. Suitable for those who use VS
    just for debugging and an alternative file opener (e.g the fzf-based rmenu). A
    run of '.sln generate' is not required. """

    def _write_primary_name(self):
        if hasattr(self, "_primary_name"):
            return self._primary_name

        ue_context = self.get_unreal_context()
        if (branch := ue_context.get_branch()) is None:
            return

        engine = ue_context.get_engine()
        name = "UE" + str(engine.get_version_major())
        name = name + "_" + ue_context.get_branch().get_name()
        ppn_path = engine.get_dir() / "Intermediate/ProjectFiles/PrimaryProjectName.txt"
        ppn_path.parent.mkdir(parents=True, exist_ok=True)
        with ppn_path.open("wt") as out:
            print(name, file=out)

    def main(self):
        ue_context = self.get_unreal_context()
        branch = ue_context.get_branch()
        if not branch:
            raise EnvironmentError("A branch ius required for a tiny solution")

        self._write_primary_name()

        projects = []
        if branch := ue_context.get_branch():
            for item in branch.read_projects():
                projects += item.parent.glob("Source/*.Target.cs")

        programs = []
        for item in ue_context.glob("Source/Programs/*"):
            programs += item.glob("*.Target.cs")

        import slnformer

        proj_dir = ue_context.get_engine().get_dir()
        proj_dir /= "Intermediate/ProjectFiles/TinySln"

        former = slnformer.Sln(self._get_sln_path(), proj_dir)

        tiny_proj = former.add_project("_tiny", makefile=False)
        tiny_proj.add_property("ushell_bin_suffix", "-Win64-$(Configuration)", cond="$(Configuration) != 'Development'")
        tiny_proj.add_property("ushell_bin_prefix", "../../../")
        tiny_proj.add_property("ushell_bin", "$(ushell_bin_prefix)Binaries/Win64/$(ushell_target)$(ushell_bin_suffix).exe")
        tiny_proj.add_property("NMakeOutput", "$(ushell_bin)")
        tiny_proj.set_build_action("cd $(ushell_dir) &amp; .build target $(ushell_target) win64 $(Configuration.ToLower())")
        tiny_proj.set_rebuild_action("cd $(ushell_dir) &amp; $(NMakeCleanCommandLine) &amp; $(NMakeBuildCommandLine)")
        tiny_proj.set_clean_action("cd $(ushell_dir) &amp; $(NMakeBuildCommandLine) --clean")
        tiny_proj.add_property("FLOW_SID", "")
        tiny_proj.add_import("$(VCTargetsPath)/Microsoft.Cpp.Default.props")
        tiny_proj.add_import("$(VCTargetsPath)/Microsoft.Cpp.props")
        tiny_proj.add_import("$(VCTargetsPath)/Microsoft.Cpp.targets")

        def prep_proj(proj, context_dir):
            target = proj.get_name().replace(".vcxproj", "")
            proj.add_import(tiny_proj.get_name())
            proj.add_property("ushell_target", target)
            proj.add_property("ushell_dir", str(context_dir))

        for project in projects:
            proj_dir = project.parent.parent
            proj_name = project.name[:-10]
            folder = former.add_folder(proj_dir.name)
            proj = folder.add_project(proj_name)
            prep_proj(proj, proj_dir)

        folder = former.add_folder("_programs")
        for program in programs:
            prog_dir = project.parent
            prog_name = program.name[:-10]
            prog_index = folder.add_folder(prog_name[0].lower())
            proj = prog_index.add_project(prog_name)
            prep_proj(proj, prog_dir)

        former.write()

        self.print_info("Creating a tiny solution");
        print("Sln path:", self._get_sln_path())
        print("Dir:", proj_dir)
        print("Projects:", len(projects))
        print("Programs:", len(programs))

        return self._open_sln()
