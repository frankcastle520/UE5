This is a work-in-progress project to set up clang-format for the Unreal Engine
codebase.

# Required binaries

Right now, you have to add clang-format.exe and clang-format-diff.py by
yourself. Download them from an LLVM release and put them into this directory.

# clang-format-diff

Clang comes with a handy script called clang-format-diff which uses
clang-format to format only the lines of code that has changed according to
your version control system.

To diff lines that have changed according to Perforce, you can use the script
perforce-clang-format-diff.ps1.

# Formatting changed lines on save in Rider

You can format files automatically using clang-format-diff when you save
changes to them from the Rider IDE.

To do this, go to Rider's "Settings > Tools > File Watchers" and add a two new
ones. Here are examples of values from a successful setup of these (the only
that change between them is "File type"):

| Field | Value |
|---|---|
| File type | C++ header files (or "C++ files" for .cpp files) |
| Scope | Open Files |
| Program | pwsh.exe |
| Arguments | -File PATH-TO-CHECKOUT\Engine\Extras\clang-format\perforce-clang-format-diff.ps1 $FilePath$ -PerformSlateFiltering |
| Working directory | $FileDir$ |

Note that this example enables the -PerformSlateFiltering flag (see below for info).

Note that the "Show console" option can be useful to debug the watcher.

# macOS support

The new `perforce-clang-format-diff.py` works on both Windows and macOS. You can set it up in Rider like this:

| Field | Value                                                                                          |
|---|------------------------------------------------------------------------------------------------|
| File type | C++ header files (or "C++ files" for .cpp files)                                               |
| Scope | Open Files                                                                                     |
| Program | python                                                                                         |
| Arguments | PATH-TO-CHECKOUT\Engine\Extras\clang-format\perforce-clang-format-diff.py $FilePath$ --verbose |
| Working directory | $FileDir$                                                                                      |

# Known issue: formatting Slate code

Slate code uses an uncommon overload of operator[] to nest widgets
declaratively. clang-format doesn't understand this and therefore cannot format
such code nicely.

An example of this problem is that this code

```
AddSlot()
[
    MyWidget
];
```

is formatted into this

```
AddSlot()[MyWidget];
```

which is not what we want for our Slate code.

To work around this issue, we can filter out any Slate code from what we give to
clang-format. This works well when using clang-format-diff, which is already
diff based.

This technique has been implemented in the perforce-clang-format-diff.ps1 script
as a filter on the output of `p4 diff` where any Slate-like code is removed from
the diff before passed onto clang-format-diff.py. Enable this filter by passing
`-PerformSlateFiltering` to perforce-clang-format-diff.ps1.

Note that in rare cases, clang-format-diff will format code surrounding a diff
regardless if the surrounding code has been changed or not. This can effectively
override `-PerformSlateFiltering`, causing Slate code to be filtered
incorrectly despite using the flag.
