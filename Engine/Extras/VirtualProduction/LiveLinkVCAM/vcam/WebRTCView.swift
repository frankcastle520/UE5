//
//  WebRTCView.swift
//  WebRTC-Demo
//
//  Created by Luke Bermingham on 14/6/2022.

import Foundation
import UIKit
import WebRTC
import MetalKit

protocol WebRTCViewDelegate : AnyObject {
    func webRTCView(_ view : WebRTCView, didChangeVideoSize size : CGSize)
}

final class WebRTCView: UIView, RTCVideoViewDelegate {
    
    weak var delegate : WebRTCViewDelegate?
    weak var videoView : RTCMTLVideoViewWithTouch?
    
    var videoSize = CGSize.zero

    override init(frame: CGRect) {
        super.init(frame: frame)
        
        let videoView = RTCMTLVideoViewWithTouch(frame: .zero)
        videoView.videoContentMode = .scaleAspectFit
        videoView.isEnabled = true
        videoView.isHidden = false
        videoView.backgroundColor = UIColor.lightGray
        
        videoView.delegate = self
        addSubview(videoView)
        self.videoView = videoView
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        
        let videoView = RTCMTLVideoViewWithTouch(frame: .zero)
        videoView.delegate = self
        addSubview(videoView)
        self.videoView = videoView
    }
    
    deinit {
        Log.info("WebRTCView destructed.")
    }

    func videoView(_ videoView: RTCVideoRenderer, didChangeVideoSize size: CGSize) {
        self.videoSize = size
        self.videoView?.touchDelegate?.onVideoChangedSize(size: size)
        setNeedsLayout()
        
        self.delegate?.webRTCView(self, didChangeVideoSize: size)
    }
    
    func attachVideoTrack(track: RTCVideoTrack) {
        if let vidView = self.videoView {
            track.remove(vidView)
            track.add(vidView)
            track.isEnabled = true
            vidView.renderFrame(nil)
            let trackState : RTCMediaStreamTrackState = track.readyState
            if trackState == RTCMediaStreamTrackState.live {
                print("Video track is live | id=\(track.trackId) | kind=\(track.kind)")
            }
        }
    }
    
    func removeVideoTrack(track: RTCVideoTrack) {
        if let vidView = self.videoView {
            track.remove(vidView)
        }
    }
    
    func attachTouchDelegate(delegate: TouchDelegate) {
        self.videoView?.touchDelegate = delegate
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        guard videoSize.width > 0 && videoSize.height > 0 else {
            videoView?.frame = bounds
            return
        }

        var videoFrame = AVMakeRect(aspectRatio: videoSize, insideRect: bounds)
        let scale = videoFrame.size.aspectFitScale(in: bounds.size)
        videoFrame.size.width = videoFrame.size.width * CGFloat(scale)
        videoFrame.size.height = videoFrame.size.height * CGFloat(scale)

        videoView?.frame = videoFrame
        videoView?.center = CGPoint(x: bounds.midX, y: bounds.midY)
    }
    
}

extension CGSize {
    func aspectFitScale(in container: CGSize) -> CGFloat {

        if height <= container.height && width > container.width {
            return container.width / width
        }
        if height > container.height && width > container.width {
            return min(container.width / width, container.height / height)
        }
        if height > container.height && width <= container.width {
            return container.height / height
        }
        if height <= container.height && width <= container.width {
            return min(container.width / width, container.height / height)
        }
        return 1.0
    }
}

class RTCMTLVideoViewWithTouch : RTCMTLVideoView {
    
    weak var touchDelegate : TouchDelegate?
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        self.isMultipleTouchEnabled = true
    }
    
    required init?(coder aDecoder: NSCoder) {
        super.init(coder: aDecoder)
        isMultipleTouchEnabled = true
    }
    
    open override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
        self.touchDelegate?.touchesBegan(touches)
    }
    
    open override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesMoved(touches, with: event)
        self.touchDelegate?.touchesMoved(touches)
    }
    
    open override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesEnded(touches, with: event)
        self.touchDelegate?.touchesEnded(touches)
    }
    
    open override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesCancelled(touches, with: event)
        self.touchDelegate?.touchesCancelled(touches)
    }
    
    func setPreferredFramerate(fps: Int) {
        for subview in self.subviews {
            if let mtlView = subview as? MTKView {
                mtlView.preferredFramesPerSecond = fps
            }
        }
    }
    
}
