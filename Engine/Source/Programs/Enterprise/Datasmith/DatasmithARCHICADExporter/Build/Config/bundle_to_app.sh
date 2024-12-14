#!/bin/sh


ConfigPath=`dirname "$0"`
pushd $ConfigPath/..
projectPath=`pwd`
cd ../../../../../..
EnginePath=`pwd`
popd

echo "Renaming *.bundle to *.app"
pushd $EnginePath/Binaries/Mac/DatasmithARCHICADExporter
mv DatasmithARCHICAD23Exporter.bundle DatasmithARCHICAD23Exporter.app
mv DatasmithARCHICAD24Exporter.bundle DatasmithARCHICAD24Exporter.app
mv DatasmithARCHICAD25Exporter.bundle DatasmithARCHICAD25Exporter.app
mv DatasmithARCHICAD26Exporter.bundle DatasmithARCHICAD26Exporter.app
mv DatasmithARCHICAD27Exporter.bundle DatasmithARCHICAD27Exporter.app
popd
