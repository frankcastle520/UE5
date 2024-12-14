// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:math' as math;

import 'package:epic_common/preferences.dart';
import 'package:epic_common/theme.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter/physics.dart';
import 'package:flutter/scheduler.dart';

import '../../models/settings/floating_window_settings.dart';

/// Sides of the screen on which the window can be docked.
enum _DockSide {
  left,
  right,
}

/// Possible states for the floating window in terms of docking.
enum _DockState {
  /// The window is on-screen and no dock tabs are visible.
  undocked,

  /// The window has entered the docking range and is revealing its tab.
  docking,

  /// The window has revealed its docking tab. It may not actually be off-screen yet.
  docked,

  /// The window has exited the docking range and is hiding its tab.
  undocking,
}

/// How far to inset the window from the edge of the screen.
const double _edgeInset = 8.0;

/// The size of the tab shown when the window is docked.
const _dockTabSize = Size(32, 80);

/// The smallest dimension of the container will be multiplied by this value to determine the height of the window.
const double _windowSizeScale = 0.25;

/// The height of the window will be multiplied by this value to determine its width.
const double _windowAspectRatio = 16.0 / 9.0;

/// Builds the contents of a floating window in the given build [context].
typedef FloatingWindowContentBuilder = Widget Function(BuildContext context);

/// A floating window that the user can drag to any corner of the screen and dock to the side.
class FloatingWindow extends StatefulWidget {
  const FloatingWindow({
    Key? key,
    required this.settingsPrefix,
    required this.icon,
    required this.builder,
    this.size = null,
    this.draggableInsets = EdgeInsets.zero,
  }) : super(key: key);

  /// The prefix used to look up the tab's specific user settings.
  final String settingsPrefix;

  /// The icon to display on the tab when docked.
  final Widget icon;

  /// The function to build the tab's contents.
  final FloatingWindowContentBuilder builder;

  /// The size of the window.
  final Size? size;

  /// When the window is undocked, the area that the user can drag will be inset by this much.
  final EdgeInsetsGeometry draggableInsets;

  /// Calculate the default size for a floating window in this [context].
  static Size getDefaultSize(BuildContext context) {
    final Size screenSize = MediaQuery.of(context).size;
    final double windowHeight = math.min(screenSize.width, screenSize.height) * _windowSizeScale;

    return Size(windowHeight * _windowAspectRatio, windowHeight);
  }

  @override
  State<FloatingWindow> createState() => _FloatingWindowState();
}

class _FloatingWindowState extends State<FloatingWindow> with SingleTickerProviderStateMixin {
  /// Physics settings for the spring simulation when the user is dragging the window.
  static const _draggedSpringDescription = SpringDescription(mass: 40, stiffness: 1, damping: 1.2);

  /// Physics settings for the spring simulation when the user releases the window.
  static const _releasedSpringDescription = SpringDescription(mass: 40, stiffness: 1, damping: 1);

  /// Physics settings for the spring simulation when the window is being docked.
  static const _dockedSpringDescription = SpringDescription(mass: 60, stiffness: 1, damping: 1);

  /// When this portion of the window's width is offscreen, dock it.
  static const double _dockThreshold = 1 / 3;

  /// Which side the window was last docked on.
  _DockSide _dockSide = _DockSide.left;

  /// State of the window window's docking.
  _DockState _dockState = _DockState.undocked;

  /// The position that the window's top-left corner should move towards with spring motion.
  Offset _targetWindowPosition = Offset.zero;

  /// The current position of the window's top-left corner.
  Offset _windowPosition = Offset.zero;

  /// The current velocity of the window.
  Offset _windowVelocity = Offset.zero;

  /// The user's last drag position in global space.
  Offset _lastDragPosition = Offset.zero;

  /// The last known size of the containing render box.
  Size _containerSize = Size.zero;

  /// The elapsed ticker time in seconds when the springs were last updated.
  double _lastSpringTime = 0;

  /// The last value of [_lastSpringTime] when the springs' target positions were moved.
  double _lastSpringMoveTime = 0;

  /// Spring controlling the X axis.
  SpringSimulation? _xSpring;

  /// Spring controlling the Y axis.
  SpringSimulation? _ySpring;

  /// Ticker used to update springs.
  late final Ticker _ticker;

  /// Settings for this window map.
  late final FloatingWindowSettings _settings;

  /// The size of the window to display given the containing screen's size.
  Size get _windowSize => widget.size ?? FloatingWindow.getDefaultSize(context);

  /// True if the window is currently docked or in the process of docking.
  bool get _bIsDockedOrDocking => _dockState == _DockState.docking || _dockState == _DockState.docked;

  /// True if the window is currently undocked or in the process of undocking.
  bool get _bIsUndockedOrUndocking => _dockState == _DockState.undocking || _dockState == _DockState.undocked;

  /// True if the window is not docked and offscreen.
  bool get _bIsWindowVisible {
    if (_dockState != _DockState.docked) {
      return true;
    }

    // Check if the window is actually within visible bounds (e.g. still moving to docked position or being dragged out
    // of it).
    return ((_windowPosition.dx + _windowSize.width - _dockTabSize.width) > 0 &&
        (_windowPosition.dx + _dockTabSize.width) < _containerSize.width);
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (BuildContext context, BoxConstraints constraints) {
      final Size newSize = Size(constraints.maxWidth, constraints.maxHeight);

      // Handle first size/resize
      if (newSize != _containerSize) {
        final Size oldSize = _containerSize;
        _containerSize = newSize;

        if (oldSize == Size.zero) {
          _initPosition();
        } else {
          WidgetsBinding.instance.addPostFrameCallback((_) => _onContainerResized(oldSize));
        }
      }

      // Determine the offset of the main window window, which shifts to the side to reveal dock tabs when docked.
      double dockOffset = 0;
      if (_bIsDockedOrDocking) {
        switch (_dockSide) {
          case _DockSide.left:
            dockOffset = -_dockTabSize.width;
            break;

          case _DockSide.right:
            dockOffset = _dockTabSize.width;
            break;
        }
      }

      return Stack(
        clipBehavior: Clip.hardEdge,
        children: [
          // Position a box containing the window, dock tabs, and gesture detector
          Positioned(
            top: _windowPosition.dy,
            left: _windowPosition.dx,
            child: SizedBox(
              width: _windowSize.width,
              height: _windowSize.height,
              child: Stack(
                clipBehavior: Clip.none,
                children: [
                  // Tab shown on the left when docked on the right
                  if (_dockState != _DockState.undocked && _dockSide == _DockSide.right)
                    Positioned(
                      key: Key('LeftDockTab'),
                      left: 0,
                      top: (_windowSize.height - _dockTabSize.height) / 2,
                      child: _DockTab(
                        side: _DockSide.right,
                        icon: widget.icon,
                      ),
                    ),

                  // Tab shown on the right when docked on the left
                  if (_dockState != _DockState.undocked && _dockSide == _DockSide.left)
                    Positioned(
                      key: Key('RightDockTab'),
                      left: _windowSize.width - _dockTabSize.width,
                      top: (_windowSize.height - _dockTabSize.height) / 2,
                      child: _DockTab(
                        side: _DockSide.left,
                        icon: widget.icon,
                      ),
                    ),

                  // Window window, which shifts to reveal a tab when docked and hides when offscreen
                  AnimatedPositioned(
                    key: Key('WindowMover'),
                    left: dockOffset,
                    duration: const Duration(milliseconds: 200),
                    child: _bIsWindowVisible
                        ? _WindowBody(
                            key: Key('Window'),
                            size: _windowSize,
                            bShadowVisible: _bIsUndockedOrUndocking,
                            child: widget.builder(context),
                          )
                        : SizedBox(),
                    onEnd: _onDockTabAnimationEnd,
                  ),

                  // Gesture detector, which matches the window rectangle when undocked and the revealed tab when docked
                  Positioned(
                    key: Key('GestureDetector'),

                    // When docked to left, offset to line up with the tab on the right side of the window
                    left: (_dockState == _DockState.docked && _dockSide == _DockSide.left)
                        ? (_windowSize.width - _dockTabSize.width)
                        : 0,

                    // When docked, offset to line up with the top of the tab
                    top: _dockState == _DockState.docked ? ((_windowSize.height - _dockTabSize.height) / 2) : 0,

                    child: SizedBox(
                      // When docked, match size to the tab
                      width: _dockState == _DockState.docked ? _dockTabSize.width : _windowSize.width,
                      height: _dockState == _DockState.docked ? _dockTabSize.height : _windowSize.height,

                      child: Padding(
                        padding: _dockState == _DockState.docked ? EdgeInsets.zero : widget.draggableInsets,
                        child: GestureDetector(
                          behavior: HitTestBehavior.opaque,
                          onTap: _onTap,
                          onPanUpdate: _updateDrag,
                          onPanEnd: (details) => _endDrag(details.velocity),
                          onPanCancel: () => _endDrag(Velocity.zero),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      );
    });
  }

  @override
  void initState() {
    super.initState();

    _ticker = createTicker(_onTick);
    _settings = FloatingWindowSettings(
      PreferencesBundle.of(context),
      widget.settingsPrefix,
    );
  }

  @override
  void dispose() {
    _ticker.dispose();

    super.dispose();
  }

  /// Initialize the position of the window once we a valid size to reference.
  /// This should NOT call setState, as it's called directly from the first build().
  void _initPosition() {
    final restoredYPosition = _settings.yAxis.getValue() * _containerSize.height;
    final FloatingWindowSide mapSide = _settings.side.getValue();

    if (_settings.bIsDocked.getValue()) {
      switch (mapSide) {
        case FloatingWindowSide.left:
          _dockSide = _DockSide.left;
          break;

        case FloatingWindowSide.right:
          _dockSide = _DockSide.right;
          break;
      }

      _dockState = _DockState.docked;
      _targetWindowPosition = _windowPosition = _getDockedWindowPosition(dockY: restoredYPosition);
    } else {
      // Make an approximate position for the window, which we'll then snap to the nearest corner
      late final double tempXPosition;
      switch (mapSide) {
        case FloatingWindowSide.left:
          tempXPosition = 0;
          break;

        case FloatingWindowSide.right:
          tempXPosition = _containerSize.width - _windowSize.width;
          break;
      }

      final tempPosition = Offset(tempXPosition, restoredYPosition);

      _dockState = _DockState.undocked;
      _targetWindowPosition = _windowPosition = _getNearestWindowCornerPosition(tempPosition);
    }
  }

  /// Called when the containing widget is resized and we need to readjust accordingly.
  void _onContainerResized(Size oldContainerSize) {
    if (_containerSize.height == 0 || _containerSize.width == 0) {
      return;
    }

    final Offset scaledPosition = Offset(
      _targetWindowPosition.dx * (_containerSize.width / oldContainerSize.width),
      _targetWindowPosition.dy * (_containerSize.height / oldContainerSize.height),
    );

    if (_bIsDockedOrDocking) {
      // Stay docked and snap to the new side
      setState(() {
        _targetWindowPosition = _windowPosition = _getDockedWindowPosition(dockY: scaledPosition.dy);
      });
    } else {
      // Move smoothly to the new position
      _moveWindowTargetToNearestCorner(scaledPosition);
    }
  }

  /// Handle a tap on the window.
  void _onTap() {
    if (_bIsUndockedOrUndocking) {
      return;
    }

    // Currently docked, so undock by moving to the nearest corner.
    _moveWindowTargetToNearestCorner(_targetWindowPosition);
  }

  /// Update the dragged window position.
  void _updateDrag(DragUpdateDetails details) {
    _lastDragPosition = details.globalPosition;

    _moveWindowTarget(
      _targetWindowPosition + Offset(details.delta.dx, details.delta.dy),
      springDescription: _draggedSpringDescription,
    );

    final _DockSide? newDockSide = _checkDockSideAtPosition(_targetWindowPosition);
    if (newDockSide != null) {
      // If this will dock, show the tab
      _dockWindow(newDockSide, moveOffscreen: false);
    } else {
      // No longer docked, so move to undocking state
      _undockWindow();
    }
  }

  /// Stop dragging the window and let it come to rest.
  void _endDrag(Velocity velocity) {
    final RenderBox? renderBox = context.findRenderObject() as RenderBox?;
    if (renderBox == null) {
      _moveWindowTarget(
        Offset(_edgeInset, _edgeInset),
        springDescription: _releasedSpringDescription,
      );
      return;
    }

    // Check ahead from the window's position so the user can "fling" the window off the screen
    const dockCheckAheadSeconds = 0.1;
    final Offset dockCheckPosition = _windowPosition + velocity.pixelsPerSecond * dockCheckAheadSeconds;

    // If near the edges, dock the window
    final _DockSide? newDockSide = _checkDockSideAtPosition(dockCheckPosition);
    if (newDockSide != null) {
      _dockWindow(newDockSide, dockY: dockCheckPosition.dy);
      return;
    }

    // Otherwise, move to the nearest corner. In this case, use the user's drag position instead of the window
    // position since flinging feels more accurate that way.
    const cornerCheckAheadSeconds = 0.15;
    final Offset cornerCheckPosition = _lastDragPosition + velocity.pixelsPerSecond * cornerCheckAheadSeconds;
    final Offset finalLocalPosition = renderBox.globalToLocal(cornerCheckPosition);

    _moveWindowTargetToNearestCorner(finalLocalPosition);
  }

  /// Update spring window position using springs.
  void _onTick(Duration elapsed) {
    if (_xSpring == null || _ySpring == null) {
      return;
    }

    _lastSpringTime = elapsed.inMilliseconds / 1000.0;

    final double springTime = _lastSpringTime - _lastSpringMoveTime;

    setState(() {
      _windowVelocity = Offset(_xSpring!.dx(springTime), _ySpring!.dx(springTime));
      _windowPosition = Offset(_xSpring!.x(springTime), _ySpring!.x(springTime));
    });

    // If both springs are at rest, stop ticking to save performance
    if (_xSpring!.isDone(springTime) && _ySpring!.isDone(springTime)) {
      _ticker.stop();
      _lastSpringMoveTime = 0;
      _lastSpringTime = 0;
    }
  }

  /// Called when the animation to reveal/hide a dock tab completes.
  void _onDockTabAnimationEnd() {
    switch (_dockState) {
      case _DockState.undocking:
        setState(() {
          _dockState = _DockState.undocked;
        });
        break;

      case _DockState.docking:
        setState(() {
          _dockState = _DockState.docked;
        });
        break;

      default:
        break;
    }
  }

  /// Move the target position for the window to a new location and adjust springs accordingly.
  void _moveWindowTarget(Offset newTarget, {required SpringDescription springDescription}) {
    // Clamp both axes in a valid range
    _targetWindowPosition = Offset(
      newTarget.dx.clamp(-_windowSize.width + _dockTabSize.width, _containerSize.width - _dockTabSize.width),
      _getClampedWindowYPosition(newTarget.dy),
    );

    _xSpring = SpringSimulation(
      springDescription,
      _windowPosition.dx,
      _targetWindowPosition.dx,
      _windowVelocity.dx,
    );

    _ySpring = SpringSimulation(
      springDescription,
      _windowPosition.dy,
      _targetWindowPosition.dy,
      _windowVelocity.dy,
    );

    _lastSpringMoveTime = _lastSpringTime;

    if (!_ticker.isActive) {
      _ticker.start();
    }

    _updateSettingsWindowMapY(newTarget.dy);
  }

  /// Move the window target to the corner nearest to a local [position].
  void _moveWindowTargetToNearestCorner(Offset position) {
    _moveWindowTarget(
      _getNearestWindowCornerPosition(position),
      springDescription: _releasedSpringDescription,
    );

    _settings.side.setValue(
      (_targetWindowPosition.dx < _containerSize.width / 2) ? FloatingWindowSide.left : FloatingWindowSide.right,
    );

    _undockWindow();
  }

  /// Reveal the dock tab and move to the docked state to the given [side].
  /// If [moveOffscreen] is true, move the window off-screen, optionally using the Y position specified by [dockY].
  void _dockWindow(_DockSide side, {bool moveOffscreen = true, double? dockY}) {
    dockY = dockY ?? _windowPosition.dy;

    if (_bIsUndockedOrUndocking) {
      // Trigger the docking animation
      setState(() {
        _dockSide = side;
        _dockState = _DockState.docking;
      });

      // Update user settings
      switch (side) {
        case _DockSide.left:
          _settings.side.setValue(FloatingWindowSide.left);
          break;

        case _DockSide.right:
          _settings.side.setValue(FloatingWindowSide.right);
          break;

        default:
          break;
      }

      _settings.bIsDocked.setValue(true);
      _updateSettingsWindowMapY(dockY);
    }

    if (!moveOffscreen) {
      return;
    }

    _moveWindowTarget(
      _getDockedWindowPosition(dockY: dockY),
      springDescription: _dockedSpringDescription,
    );
  }

  /// Hide the docked tab and move to the undocking state.
  void _undockWindow() {
    if (_bIsDockedOrDocking) {
      setState(() {
        _dockState = _DockState.undocking;
      });

      _settings.bIsDocked.setValue(false);
    }
  }

  /// Check whether the window should be docked if it's at the given X position.
  /// Returns the side to dock to, or null if it shouldn't dock.
  _DockSide? _checkDockSideAtPosition(Offset position) {
    if (position.dx + ((1 - _dockThreshold) * _windowSize.width) > _containerSize.width) {
      return _DockSide.right;
    }

    if (position.dx + (_dockThreshold * _windowSize.width) < 0) {
      return _DockSide.left;
    }

    return null;
  }

  /// Update the saved Y position of the docked window.
  void _updateSettingsWindowMapY(double dockY) {
    _settings.yAxis.setValue(dockY / _containerSize.height);
  }

  /// Clamp a Y position for the window to a valid range.
  double _getClampedWindowYPosition(double y) {
    final double minY = _edgeInset;
    final double maxY = _containerSize.height - _windowSize.height - _edgeInset;

    return math.max(math.min(y, maxY), minY);
  }

  /// Get the position of the window when moved to the nearest corner to a local [position].
  Offset _getNearestWindowCornerPosition(Offset position) {
    late final double targetX;
    if (position.dx < _containerSize.width / 2) {
      targetX = _edgeInset;
    } else {
      targetX = _containerSize.width - _windowSize.width - _edgeInset;
    }

    late final double targetY;
    if (position.dy < _containerSize.height / 2) {
      targetY = _edgeInset;
    } else {
      targetY = _containerSize.height - _windowSize.height - _edgeInset;
    }

    return Offset(targetX, targetY);
  }

  /// Get the docked position of the window when its Y position is [dockY].
  Offset _getDockedWindowPosition({required double dockY}) {
    // Extra amount to move just to make sure it's fully off-screen so we can stop rendering the window
    const double offsetFudge = 0.5;

    // Move the window to the appropriate side
    late final double targetX;
    switch (_dockSide) {
      case _DockSide.left:
        targetX = -_windowSize.width + _dockTabSize.width - offsetFudge;
        break;

      case _DockSide.right:
        targetX = _containerSize.width - _dockTabSize.width + offsetFudge;
        break;
    }

    return Offset(targetX, _getClampedWindowYPosition(dockY));
  }
}

/// The main body of the window which moves around within the larger widget.
class _WindowBody extends StatelessWidget {
  const _WindowBody({
    Key? key,
    required this.size,
    required this.bShadowVisible,
    required this.child,
  }) : super(key: key);

  /// The size of the window's body.
  final Size size;

  /// If true, add a shadow effect to the window's body.
  final bool bShadowVisible;

  /// The widget contained within the window's body.
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final borderRadius = BorderRadius.circular(16);

    return SizedBox(
      width: size.width,
      height: size.height,
      child: Stack(
        children: [
          Positioned.fill(
            child: AnimatedContainer(
              clipBehavior: Clip.antiAlias,
              duration: Duration(milliseconds: 400),
              decoration: BoxDecoration(
                borderRadius: borderRadius,
                boxShadow: [
                  BoxShadow(
                    color: Color(0xff000000).withOpacity(bShadowVisible ? 0.4 : 0),
                    blurRadius: 5,
                    blurStyle: BlurStyle.outer,
                  ),
                ],
              ),
              child: child,
            ),
          ),
          IgnorePointer(
            child: Container(
              decoration: BoxDecoration(
                borderRadius: borderRadius,
                border: Border.all(
                  color: UnrealColors.gray56,
                  width: 2,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// The tab shown when the window is docked.
class _DockTab extends StatelessWidget {
  const _DockTab({
    required this.side,
    required this.icon,
  });

  /// Which side the window is docked to.
  final _DockSide side;

  /// The icon to display on the tab when docked.
  final Widget icon;

  @override
  Widget build(BuildContext context) {
    const Radius borderRadius = Radius.circular(8);
    final bool bIsFacingLeft = side == _DockSide.right;

    return Container(
      width: _dockTabSize.width,
      height: _dockTabSize.height,
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceTint,
        borderRadius: bIsFacingLeft
            ? BorderRadius.only(
                topLeft: borderRadius,
                bottomLeft: borderRadius,
              )
            : BorderRadius.only(
                topRight: borderRadius,
                bottomRight: borderRadius,
              ),
        boxShadow: [
          BoxShadow(
            color: Color(0x40000000),
            offset: Offset(bIsFacingLeft ? -4 : 4, 4),
            blurRadius: 4,
          ),
        ],
      ),
      child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
        SizedBox.square(
          dimension: 24,
          child: icon,
        ),
        SizedBox(
          height: 6,
        ),
        AssetIcon(
          path: bIsFacingLeft
              ? 'packages/epic_common/assets/icons/chevron_left.svg'
              : 'packages/epic_common/assets/icons/chevron_right.svg',
          size: 24,
        ),
      ]),
    );
  }
}
