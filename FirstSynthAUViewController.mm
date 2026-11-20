#import "IPlugAUAudioUnit.h"
#import <CoreAudioKit/AUViewController.h>

@interface FirstSynthAUContainerView : NSView
@property(nonatomic, copy) void (^sizeChanged)(NSSize);
@end
@implementation FirstSynthAUContainerView
- (void)setFrame:(NSRect)frame
{
  BOOL changed = !NSEqualSizes(frame.size, self.frame.size);
  [super setFrame:frame];
  if (changed && self.sizeChanged) self.sizeChanged(frame.size);
}

- (void)setFrameSize:(NSSize)size
{
  BOOL changed = !NSEqualSizes(size, self.frame.size);
  [super setFrameSize:size];
  if (changed && self.sizeChanged) self.sizeChanged(size);
}
@end

// The iPlug controller pins constant width/height constraints on the child.
// This controller uses the host's frame as the source of truth instead, so a
// resize cannot feed back into Auto Layout and reset the host window size.
@interface FirstSynthAUViewController : AUViewController <AUAudioUnitFactory>
@property(nonatomic, strong) IPLUG_AUAUDIOUNIT* audioUnit;
@end

@implementation FirstSynthAUViewController
{
  NSView* mPlugView;
  NSSize mLastSize;
}

- (void)loadView
{
  FirstSynthAUContainerView* view = [[FirstSynthAUContainerView alloc]
    initWithFrame:NSMakeRect(0,0,1110,606)];
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  __weak FirstSynthAUViewController* weakSelf = self;
  view.sizeChanged = ^(NSSize size) { [weakSelf notifyHostSize:size]; };
  self.view = view;
}

- (AUAudioUnit*)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc error:(NSError**)error
{
  self.audioUnit = [[IPLUG_AUAUDIOUNIT alloc] initWithComponentDescription:desc error:error];
  if (self.audioUnit) {
    dispatch_async(dispatch_get_main_queue(), ^{
      NSSize size = NSMakeSize(round([self.audioUnit width]*0.8), round([self.audioUnit height]*0.8));
      (void)self.view;
      self.preferredContentSize = size;
      [self.view setFrameSize:size];
      if (self.view.window && !self->mPlugView) [self openEditor];
    });
  }
  return self.audioUnit;
}

- (void)openEditor
{
  if (mPlugView || !self.audioUnit) return;
  mPlugView = [self.audioUnit openWindow:self.view];
  mPlugView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  mLastSize = NSZeroSize;
  [self notifyHostSize:self.view.bounds.size];
}

- (void)viewWillAppear
{
  [super viewWillAppear];
  [self openEditor];
}

- (void)notifyHostSize:(NSSize)size
{
  if (!mPlugView || size.width <= 0 || size.height <= 0 || NSEqualSizes(size,mLastSize)) return;
  mLastSize = size;
  [self.audioUnit hostResized:size];
}

- (void)viewDidDisappear
{
  if (mPlugView) {
    [self.audioUnit closeWindow];
    [mPlugView removeFromSuperview];
    mPlugView = nil;
  }
  [super viewDidDisappear];
}
@end
