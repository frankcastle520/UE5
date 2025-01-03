from PySide6 import QtGui

import sys
import os
import logging

import vrScenegraph
import vrFileIO
import vrFileDialog
import vrNodeUtils
import vrFieldAccess
import vrNodePtr
import vrKernelServices

# Adds the ScriptPlugins folder to path so that we can find the other files
# in our module
scriptPluginsFolder = os.path.dirname(os.path.realpath(os.curdir))
if scriptPluginsFolder not in sys.path:
    sys.path.append(scriptPluginsFolder)

def isSharedNode(node):
    """
    A node is shared if its straight up a cloned node, or a transformable root, which is
    a special type of Transform node that can be used to independently move cloned nodes
    """
    APIV1Node = vrNodePtr.toNode(node.getObjectId())
    if ((vrScenegraph.isCloned(APIV1Node) or APIV1Node.hasAttachment('SynchronizationAttachment')) != node.isShared()):
        logging.warning("################### " + str(vrScenegraph.isCloned(APIV1Node)) + " OR " + str(APIV1Node.hasAttachment('SynchronizationAttachment')) + " should be " + str(node.isShared()))
    #return vrScenegraph.isCloned(node) or node.hasAttachment('SynchronizationAttachment')
    #API V2 is isShared enough to replace the "isCloned or hasAttachment" test form API V1 => apparently, we got error unsharing nodes, "You can't unshare this child node of a transformable clone root, you have to unshare the transformable clone root."
    #return node.isShared()
    return (vrScenegraph.isCloned(APIV1Node) or APIV1Node.hasAttachment('SynchronizationAttachment'))

def isNURBS(node):
    if node is not None:
        #API V2 convert new vrdNode to old vrNodePtr
        APIV1Node = vrNodePtr.toNode(node.getObjectId())
        nodeType = APIV1Node.getType()
        if nodeType == 'Surface':
            if not node.isType(vrKernelServices.vrdSurfaceNode):
                logging.warning("################### isNURBS: value should be true: " + str(node.isType(vrKernelServices.vrdSurfaceNode)))
            return True
        elif nodeType == 'Geometry':
            if not node.isType(vrKernelServices.vrdGeometryNode):
                logging.warning("################### isNURBS: value should be true: " + str(node.isType(vrKernelServices.vrdGeometryNode)))
            nodeFields = APIV1Node.fields()
            #API V2 how do we migrate hasField in API V2?
            if nodeFields.hasField('geometryType'):
                return nodeFields.getUInt32("geometryType") == 2
        elif node.isType(vrKernelServices.vrdSurfaceNode) or node.isType(vrKernelServices.vrdGeometryNode):
            logging.warning("################### isNURBS: should not be here")

    return False

def flatClone(node):
    children = node.children
    node.children.clear()
    clone = vrScenegraphService.cloneNodes([node])
    node.append(children)
    return clone

def resetTransformNode(node):
    vrNodeUtils.setTransformNodeEulerRotationOrder(node, 'xyz')
    vrNodeUtils.setTransformNodeRotatePivot(node, 0, 0, 0, False)
    vrNodeUtils.setTransformNodeRotatePivotTranslation(node, 0, 0, 0)
    vrNodeUtils.setTransformNodeRotation(node, 0, 0, 0)
    vrNodeUtils.setTransformNodeRotationOrientation(node, 0, 0, 0)
    vrNodeUtils.setTransformNodeScale(node, 1, 1, 1)
    vrNodeUtils.setTransformNodeScalePivot(node, 0, 0, 0, False)
    vrNodeUtils.setTransformNodeScalePivotTranslation(node, 0, 0, 0)
    vrNodeUtils.setTransformNodeShear(node, 0, 0, 0)
    vrNodeUtils.setTransformNodeTranslation(node, 0, 0, 0, False)

def bruteForce(node, filePath):
    """
    Uses a dictionary text file containing potential field names (one per line)
    to guess all the field names that a node has, returning them in a list.
    """
    words = []
    with open(filePath, 'r') as f:
        words = f.readlines()

    words = [x.strip() for x in words]

    result = []
    for word in words:
        if node.hasField(word):
            result.append(word)

    return result

def isClose(a, b, rel_tol=1e-09, abs_tol=0.0):
    return abs(a-b) <= max(rel_tol * max(abs(a), abs(b)), abs_tol)

def writeXMLTag(file, indent, name, attributes, values, close=False):
    """
    Writes an XML tag line to 'file', including a newline at the end. Examples:

    Input: writeXMLTag(outfile, 2, "TagName", [attr1, attr2], [val1, val2], False)
    Output:   <TagName attr1="val1" attr2="val2">\n

    Input: writeXMLTag(outfile, 1, "/TagName", [], [], False)
    Output:  </TagName>\n

    Input: writeXMLTag(outfile, 0, "TagName", [attr1], [[1, 2, 3]], True)
    Output: <TagName attr1="[1, 2, 3]"/>\n

    Args:
        file (file): File to write the tag to, as a new line
        indent (int): Number of spaces before the opening '<'
        name (str): Name of the XML node
        attributes (list of obj): List of attribute names
        values (list of obj): List of corresponding attribute values
        close (bool): Whether to write a '/' before the closing '>'
    """
    file.write(''.join([' ']*indent) + '<' + str(name))

    for pair in zip(attributes, values):
        file.write(' ' + str(pair[0]) + '="' + str(pair[1]) + '"')

    if close:
        file.write('/')

    file.write('>\n')

def getNumSceneNURBs():
    count = 0
    for node in vrScenegraphService.getAllNodes():
        if isNURBS(node):
            count += 1
    return count

def save():
    """
    Saves the current scene to a .vpb in the current opened file folder (saving on top of the scene
    if it can). If the scene hasn't been saved yet, it will prompt a save dialog.
    """
    currentScenePath = vrFileIO.getFileIOFilePath()

    if len(currentScenePath) == 0:
        currentScenePath = vrFileDialog.getSaveFileName("Save As", "", ["vpb(*.vpb)"], True)

    # Force VPB extension
    currentScenePath = os.path.splitext(currentScenePath)[0] + '.vpb'

    return currentScenePath, vrFileIO.save(currentScenePath)

def getQImageFormat(bytesPerPixel, numChannels):
    # The current version of PySide2 doesn't support all latest formats like RGBA64 or Grayscale2
    imgFormat = QtGui.QImage.Format_Invalid
    if bytesPerPixel == 1 and numChannels == 1:
        imgFormat = QtGui.QImage.Format_Grayscale8
    elif bytesPerPixel == 3 and numChannels == 3:
        imgFormat = QtGui.QImage.Format_RGB888
    elif bytesPerPixel == 4 and numChannels == 4:
        imgFormat = QtGui.QImage.Format_RGBA8888
    return imgFormat

def getUniqueFilename(fullPath):
    """
    Appends number suffixes to generate an unique path
    e.g. From C:/File.txt it might generate C:/File_2.txt
    """
    pathNoExt, ext = os.path.splitext(fullPath)

    newFullPath = fullPath
    appendedIndex = 0
    while os.path.exists(newFullPath):
        appendedIndex += 1
        newFullPath = str(pathNoExt) + '_' + str(appendedIndex) + str(ext)

    return newFullPath

def getAsFieldContainerPtr(node):
    """
    Returns a vrFieldAccess object that allows access to properties outside the
    object core. For example exposing the 'flipped' flag of AnimNodes.

    Might be None
    """
    #API V2 convert new vrdNode to old vrNodePtr
    APIV1Node = vrNodePtr.toNode(node.getObjectId())
    if (APIV1Node.getID() != node.getObjectId()):
        logging.warning("################### " + str(APIV1Node.getID()) + " should be same than " + str(node.getObjectId()))
    thisID = APIV1Node.getID()

    # Assumes that Id and container are returned in the same order, which seems to be
    # the case but I haven't seen any guarantees of
    # This trick exploits the fact that each node has its core pointing to the node itself
    # as the parent, at least. When we retrieve the parent, we can get a fieldcontainer though,
    # allowing access to other properties of a vrNodePtr
    #API V2 convert new vrdNode to old vrNodePtr
    pairs = zip(APIV1Node.fields().getMFieldContainerId('parents'), APIV1Node.fields().getMFieldContainer('parents'))
    for p in pairs:
        Id = p[0]
        fa = vrFieldAccess.vrFieldAccess(p[1])

        if Id == thisID:
            return fa
    return None

def getIsNodeFlipped(node):
    fcp = getAsFieldContainerPtr(node)
    try:
        if fcp.getBool('flipped'):
            return True
    except BaseException as e:
        logging.warning(e.message)

    return False

def zip_longest(list1, list2, fillvalue=''):
    if sys.version_info > (3, 0):
        from itertools import zip_longest
        return zip_longest(list1, list2, fillvalue=fillvalue)
    else:
        from itertools import izip_longest
        return izip_longest(list1, list2, fillvalue=fillvalue)

def dict_items_gen(dictionary):
    if sys.version_info > (3, 0):
        for pair in dictionary.items():
            yield pair
    else:
        for pair in dictionary.iteritems():
            yield pair