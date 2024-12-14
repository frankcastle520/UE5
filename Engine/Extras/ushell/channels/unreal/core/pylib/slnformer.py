from typing import Iterable, Optional, IO
from pathlib import Path

#-------------------------------------------------------------------------------
class _XmlNode(object):
    def __init__(self, parent:Optional["_XmlNode"]=None) -> None:
        self._parent = parent
        self._depth = parent._depth + 1 if parent else -1

    def get_out(self) -> IO:
        if self._parent:
            return self._parent.get_out()

    def __getattr__(self, tag:str) -> None:
        assert tag[0] != "_"
        def thunk(text:str|None=None, **attrs) -> "_XmlTag":
            return _XmlTag(self, tag, text, **attrs)
        return thunk

#-------------------------------------------------------------------------------
class _XmlTag(_XmlNode):
    def __init__(self, parent:_XmlNode, tag:str, text:str|None, **attrs) -> None:
        super().__init__(parent)
        self._tag = tag
        self._text = text
        self._write(" " * self._depth, "<", tag, end="")
        for k,v in attrs.items():
            self._write(" ", k, rf'="{v}"', end="")

    def __del__(self):
        if self._tag:
            suffix = "/>" if self._text is None else f">{self._text}</{self._tag}>"
            self._write(suffix)

    def _write(self, *args, **kwargs):
        print(*args, **kwargs, sep="", file=self.get_out())

    def __enter__(self):
        self._write(">")
        return self

    def __exit__(self, *exc):
        self._write(" " * self._depth, f"</{self._tag}>")
        self._tag = None

#-------------------------------------------------------------------------------
class _Xmler(_XmlNode):
    def __init__(self, out:IO) -> None:
        super().__init__()
        print(r'<?xml version="1.0" encoding="utf-8"?>', file=out)
        self._out = out

    def get_out(self) -> IO:
        return self._out



#-------------------------------------------------------------------------------
class _Node(object):
    guid_count = 0

    def __init__(self, name:str, parent:"_Node") -> None:
        self._parent = parent
        self._name = name
        self._guid = f"{{0A9E0A9E-0A9E-0A9E-0A9E-0A9E0A9E{_Node.guid_count:04d}}}"
        _Node.guid_count += 1

    def get_name(self) -> str:       return self._name
    def get_guid(self) -> str:       return self._guid
    def get_parent(self) -> "_Node": return None if self._parent == self else self._parent

#-------------------------------------------------------------------------------
class _Project(_Node):
    def __init__(self, name:str, parent:"_Folder") -> None:
        super().__init__(name, parent)
        self._props = []
        self._imports = { "pre" : [], "post" : [] }

    def set_build_action(self, cmd:str, *, cond:str="") -> None:
        return self.add_property("NMakeBuildCommandLine", cmd, cond=cond)

    def set_rebuild_action(self, cmd:str, *, cond:str="") -> None:
        return self.add_property("NMakeReBuildCommandLine", cmd, cond=cond)

    def set_clean_action(self, cmd:str, *, cond:str="") -> None:
        return self.add_property("NMakeCleanCommandLine", cmd, cond=cond)

    def add_property(self, name:str, value:str, *, cond:str="") -> None:
        self._props.append((name, value, cond))

    def add_import(self, imp_path:Path, *, pre:bool=False) -> None:
        self._imports["pre" if pre else "post"].append(str(imp_path))

    def write(self, out:IO) -> None:
        cond_props = {}
        for k,v,c in self._props:
            prop = (k, v)
            cond_props.setdefault(c, []).append(prop)

        xmler = _Xmler(out)
        with xmler.Project(DefaultTargets="Build", xmlns="http://schemas.microsoft.com/developer/msbuild/2003") as node:
            # TODO: formalise this!
            node._write( " <ItemGroup>")
            node._write(r'  <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>')
            node._write(r'  <ProjectConfiguration Include="Development|x64"><Configuration>Development</Configuration><Platform>x64</Platform></ProjectConfiguration>')
            node._write( " </ItemGroup>")

            for imp in self._imports["pre"]:
                node.Import(Project=imp)

            for cond, props in cond_props.items():
                kwargs = {"Condition" : cond} if cond else {}
                with node.PropertyGroup(**kwargs) as prop_grp:
                    for k, v in props:
                        getattr(prop_grp, k)(v)

            for imp in self._imports["post"]:
                node.Import(Project=imp)

#-------------------------------------------------------------------------------
class _Folder(_Node):
    def __init__(self, name:str, parent:_Node) -> None:
        self._children = []
        self._folders = {}
        super().__init__(name, parent)

    def add_folder(self, name:str) -> "_Folder":
        if name in self._folders:
            return self._folders[name]
        ret = _Folder(name, self)
        self._children.append(ret)
        self._folders[name] = ret
        return ret

    def add_project(self, name:str, makefile:bool=True) -> _Project:
        ext = ".vcxproj" if makefile else ".proj"
        ret = _Project(name + ext, self)
        if makefile:
            ret.add_property("ConfigurationType", "Makefile")
            ret.add_property("ProjectGuid", ret.get_guid())
        self._children.append(ret)
        return ret

    def read_children(self) -> Iterable[_Node]:
        for child in self._children:
            yield child
            if isinstance(child, _Folder):
                yield from child.read_children()

    def read_folders(self) -> Iterable["_Folder"]:
        return (x for x in self.read_children() if isinstance(x, _Folder))

    def read_projects(self) -> Iterable[_Project]:
        return (x for x in self.read_children() if isinstance(x, _Project))

#-------------------------------------------------------------------------------
class Sln(_Folder):
    def __init__(self, sln_path:Path, proj_dir:Path) -> None:
        super().__init__("", self)
        self._sln_path = sln_path
        self._proj_dir = proj_dir

    def write(self):
        for item in (self._sln_path.parent, self._proj_dir):
            item.mkdir(parents=True, exist_ok=True)

        for project in self.read_projects():
            proj_path = self._proj_dir / project.get_name()

            with proj_path.open("wt") as out:
                project.write(out)

        with self._sln_path.open("wt") as out:
            write = lambda *x: print(*x, sep="", file=out)

            sln_header = (
                "Microsoft Visual Studio Solution File, Format Version 12.00",
                "# Visual Studio Version 17",
                "VisualStudioVersion = 17.0.0.0",
                "MinimumVisualStudioVersion = 10.0.0.0",
            )
            sln_files = (
                # TODO: formalise this!
                r'Project("{2150E333-8FDC-42A3-9474-1A3956D46DE8}") = "__sln_files", "__sln_files", "{51012800-7858-4A8D-BEF1-3878E3C2D44B}"',
                "\tProjectSection(SolutionItems) = preProject",
                "\t\tEngine/Extras/VisualStudioDebugging/Unreal.natvis = Engine/Extras/VisualStudioDebugging/Unreal.natvis",
                "\tEndProjectSection",
                "EndProject",
            )
            sln_project = (
                r'Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "{stem}", "{path}", "{guid}"',
                # "\tProjectSection(ProjectDependencies) = postProject",
                # "\t\t{guid} = {guid}",
                # "\tEndProjectSection",
                "EndProject",
            )
            sln_folder = (
                r'Project("{{2150E333-8FDC-42A3-9474-1A3956D46DE8}}") = "{name}", "{name}", "{guid}"',
                "EndProject",
            )
            sln_nest = (
                "\tGlobalSection(NestedProjects) = preSolution",
                # {folder_guid} = {child_guid}
                "\tEndGlobalSection",
            )
            sln_global = (
                "Global",
                "EndGlobal",
            )

            for line in sln_header: write(line)
            for line in sln_files:  write(line)

            for folder in self.read_folders():
                guid = folder.get_guid()
                name = folder.get_name()
                for line in sln_folder:
                    write(line.format(guid=guid, name=name))

            proj_dir = self._proj_dir.relative_to(self._sln_path.parent)
            for project in self.read_projects():
                name = project.get_name()
                if ".vcxproj" not in name:
                    continue

                fmt_args = {
                    "stem" : Path(name).stem,
                    "guid" : project.get_guid(),
                    "path" : str(proj_dir / name),
                }
                for line in sln_project:
                    write(line.format(**fmt_args))

            write(sln_global[0])

            write(sln_nest[0])
            for item in self.read_children():
                parent = item.get_parent()
                if isinstance(parent, _Folder):
                    write("\t\t", item.get_guid(), " = ", parent.get_guid());
            write(sln_nest[1])

            write(sln_global[1])
