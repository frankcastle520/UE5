[CmdletBinding(SupportsShouldProcess)]
param (
    [Parameter(ValueFromPipeline = $true, Position = 0)]
    [String]
    $Path,

    [Parameter()]
    [string]
    $Changelist,

    [Parameter()]
    [switch]
    $PerformSlateFiltering
)

begin {
    Set-StrictMode -Version 3
    $FilesToFormat = @()
}
process {
    if (![string]::IsNullOrEmpty($Path)) {
        $FilesToFormat += $Path
    }
}
end {
    function ProcessZTag {
        param (
            [Parameter(ValueFromPipeline)]
            [String]
            $Line 
        )
        process {
            switch -regex ($Line) {
                "^\.\.\. depotFile (.*)" { $DepotFile = $Matches[1] }
                "^\.\.\. type (.*)" { $Type = $Matches[1] }
                "^\.\.\. action (.*)" { $Action = $Matches[1] }
                "^\s*$" { 
                    if ($Type -like "*text*") { 
                        Write-Verbose "Found text file $DepotFile action $Action"
                        [PSCustomObject]@{
                            DepotPath = $DepotFile
                            LocalPath = $null
                            IsAdd     = $Action -like "add"
                        }
                    }
                    else {
                        Write-Verbose "Skipping non-text file $DepotFile"
                    }
                    $DepotFile = $null
                    $Type = $null
                    $Action = $null
                }
            }
        }
    }

    function filterSlateCodeFromDiffOutput($diffOutput) {
        Write-Verbose "Performing Slater filtering by skipping formatting on parts of the code that looks like declarative Slate code."

        $fileContent = Get-Content $LocalPath
        $fileContentLines = $fileContent -split "\r?\n|\r"

        # Build zones.
        $currentStartLineIndex = -1
        $currentBracketNesting = 0
        $slateBracketZones = for ($i=0; $i -lt $fileContentLines.Length; $i++)
        {
            $line = $fileContentLines[$i]
            if ($line.Trim() -eq "[")
            {
                $currentBracketNesting += 1
                # We don't record the start of a nested Slate bracket.
                if ($currentBracketNesting -gt 0)
                {
                    $currentStartLineIndex = $i
                }
            }
            elseif ($line.Trim() -eq "]" -or $line.Trim() -eq "];")
            {
                $currentBracketNesting -= 1
                if ($currentBracketNesting -eq 0)
                {
                    $start = $currentStartLineIndex
                    $currentStartLineIndex = -1
                    @{ "startLineIndex" = [int]$start; "endLineIndex" = [int]$i };
                }
            }
        }

        # Expand zones up and down to the nearest whitespace-only lines.
        foreach ($zone in $slateBracketZones)
        {
            $startIndex = $zone["startLineIndex"]
            $endIndex = $zone["endLineIndex"]

            $originalstartIndex = $startIndex
            $originalEndIndex = $endIndex

            # Keep moving the start of the zone up until we find a whitespace-only line or the start of the file.
            while (($startIndex -gt 0) -and ($fileContentLines[$startIndex - 1].Trim() -ne ""))
            {
                $startIndex--
            }

            # Keep moving the end of the zone down until we find a whitespace-only line or the end of the file.
            while (($endIndex -lt $fileContentLines.Length-1) -and ($fileContentLines[$endIndex + 1].Trim() -ne ""))
            {
                $endIndex++
            }

            if (($originalStartIndex -ne $startIndex) -or ($originalEndIndex -ne $endIndex))
            {
                Write-Verbose "Expanded Slate bracket zone from ($originalStartIndex, $originalEndIndex) to ($startIndex, $endIndex)"
            }

            $zone["startLineIndex"] = $startIndex
            $zone["endLineIndex"] = $endIndex
        }

        $skipCurrentHunk = $false
        $diffLines = $diffOutput -split "\r?\n|\r"
        for ($i=0; $i -lt $diffLines.Length; $i++)
        {
            $diffLine = $diffLines[$i]
            if ($diffLine -match "^@@.*\+(\d+)(?:,(\d+))?")
            {
                $skipCurrentHunk = $false

                $match = $Matches

                $hunkStartLine = [int]$match.1
                $hunkLineCount = [int]$match.2
                if (-Not $hunkLineCount)
                {
                    $hunkLineCount = 1
                }
                $hunkEndLine = $hunkStartLine + $hunkLineCount - 1

                $hunkStartLineIndex = $hunkStartLine - 1
                $hunkEndLineIndex = $hunkEndLine - 1

                foreach ($slateBracketZone in $slateBracketZones)
                {
                    $zoneStart = $slateBracketZone["startLineIndex"]
                    $zoneEnd = $slateBracketZone["endLineIndex"]

                    $hunkStartInside = ($zoneStart -le $hunkStartLineIndex) -and ($hunkStartLineIndex -le $zoneEnd)
                    $hunkEndInside = ($zoneStart -le $hunkEndLineIndex) -and ($hunkEndLineIndex -le $zoneEnd)

                    if ($hunkStartInside -or $hunkEndInside)
                    {
                        Write-Verbose "Diff hunk overlaps Slate bracket zone, skipping the following lines:"
                        $skipCurrentHunk = $true
                    }
                }
            }

            if ($skipCurrentHunk)
            {
                Write-Verbose "SKIPPING: $diffLine"
            }
            else
            {
                Write-Output $diffLine
            }
        }
    }

    # Filter files on the command line to those actually present in perforce and convert to depot paths 
    if (0 -ne $FilesToFormat.Length) {
        Write-Verbose "Checking perforce status of requested files $FilesToFormat"
        [array]$DepotFiles = p4 "-ztag" "fstat" "-Ro" @FilesToFormat | ProcessZTag
    }
    else {
        if (![string]::IsNullOrEmpty($Changelist)) {
            Write-Verbose "Requested files for changelist $Changelist"
            # Get opened files in a specific changelist
            $P4Args = @("-ztag", "opened", "-c", $Changelist);
        }
        else {
            Write-Verbose "Requested files for all pending changelists"
            # Get current client name
            switch -regex (p4 "-ztag" "info") {
                "^\.\.\. clientName (.*)" { $ClientName = $Matches[1] }
            }
            if ([string]::IsNullOrEmpty($ClientName)) {
                throw "Unable to get perforce client name"
            }
            # Get all opened files
            $P4Args = @("-ztag", "opened", "-C", $ClientName);
        }

        Write-Verbose "Fetching open files from Perforce $P4Args"
        [array]$DepotFiles = p4 @P4Args | ProcessZTag
    }
    
    if ($DepotFiles.Length -eq 0) {
        throw "No files to format"
    }
    $Index = 0
    switch -regex (p4 -ztag "where" @($DepotFiles | Select-Object -ExpandProperty "DepotPath")) { 
        "^\.\.\. path (.*)" {
            $DepotFiles[$Index].LocalPath = $Matches[1]
            ++$Index
        }
    }

    # We need this to use the function from within Foreach-Object.
    $filterSlateCodeFromDiffOutputSource = ${function:filterSlateCodeFromDiffOutput}.ToString()

    $Root = $PSScriptRoot
    $DepotFiles | Foreach-Object -ThrottleLimit 5 -Parallel {
        $VerbosePreference = $using:VerbosePreference
        $WhatIfPreference = $using:WhatIfPreference
        $PerformSlateFiltering = $using:PerformSlateFiltering
        ${function:filterSlateCodeFromDiffOutput} = $using:filterSlateCodeFromDiffOutputSource
        $Root = $using:Root
        $LocalPath = $_.LocalPath
        $IsAdd = $_.IsAdd

        if ([string]::IsNullOrEmpty($LocalPath)) {
            throw "Unexpected empty path"
        }
        if ($false -eq (Test-Path $LocalPath)) {
            Write-Verbose "Skipping non-existed (deleted/moved?) file $LocalPath"
            return;
        }
        
        $clangFormatPath = Join-Path $Root "clang-format.exe"
        $formatLocalPath = Join-Path $Root "experimental.clang-format"
        # For newly added files, just format the entire file 
        if ($IsAdd) {
            if ($WhatIfPreference) {
                Write-Host "Skipping full format of newly added file $LocalPath"
            }
            else {
                & $clangFormatPath "-style=file:$formatLocalPath" "-i" $LocalPath | Out-Null 
            }
        }
        else {
            $clangFormatDiffPath = Join-Path $Root "clang-format-diff.py"

            $diffOutput = p4 "diff" "-du0" $LocalPath 

            if ($PerformSlateFiltering) {
                $diffOutput = filterSlateCodeFromDiffOutput $diffOutput
                Write-Verbose "Final diff given to clang-format-diff:"
                foreach ($line in $diffOutput) {
                    Write-Verbose $line
                }
            }

            # Check output is what we expect and bail if not 
            if ($diffOutput.Length -lt 2) {
                throw "Diff output for $LocalPath unexpectedly short"
                $diffOutput | Write-Host
            }
            if ($diffOutput[0] -notlike "---*") {
                throw "Missing old file path in diff output" 
            }
            if ($diffOutput[1] -notlike "+++*") {
                throw "Missing new file path in diff output" 
            }

            $pythonArgs = @($clangFormatDiffPath, "-binary=$clangFormatPath", "-style=file:$formatLocalPath", "-v", "-i")

            if ($WhatIfPreference) {
                Write-Host "Skipping running diff script for file $LocalPath"
                $diffOutput | Select-Object -first 2 | Write-Host
            }
            else {
                $diffFormatOutput = Write-Output $diffOutput | python @pythonArgs | Out-String
                $diffFormatExitCode = $LASTEXITCODE

                if ($diffFormatExitCode -ne 0) {
                    throw "Failed to run clang-format-diff.py on $LocalPath.`nclang-format-diff.py output:`n$diffFormatOutput"
                }
            }
        }
    }
}

