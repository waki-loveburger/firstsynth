#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <WebKit/WebKit.h>

static WKWebView* FindWebView(NSView* root)
{
  if ([root isKindOfClass:WKWebView.class]) return (WKWebView*)root;
  for (NSView* child in root.subviews) {
    WKWebView* found = FindWebView(child);
    if (found) return found;
  }
  return nil;
}

static void Pump(double seconds)
{
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}

// The refit runs off WebKit's resize event, which lands a rendering update or
// two after the frame change, later still when the machine is busy. Evaluate
// `js` and hand the result to `fits`, polling for up to 3 s instead of
// measuring once at a fixed delay; log the last numbers either way.
static BOOL PollUntil(WKWebView* web, NSString* js, NSString* what, BOOL (^fits)(id))
{
  id last = nil;
  for (int i = 0; i < 60; ++i) {
    __block BOOL done = NO, ok = NO;
    __block id result = nil;
    [web evaluateJavaScript:js completionHandler:^(id r, NSError* e) {
      result = e ? [e description] : r;
      ok = !e && fits(r);
      done = YES;
    }];
    for (int j = 0; !done && j < 500; ++j) Pump(0.01);
    last = result;
    if (done && ok) { NSLog(@"%@: %@", what, last); return YES; }
    Pump(0.05);
  }
  NSLog(@"%@: no fit after 3 s, last %@", what, last);
  return NO;
}

// Run against the built framework: simulate hosts that load the view before
// creating their audio unit on a worker thread (the AUv3 out-of-process order).
int main(int argc, const char* argv[])
{
  @autoreleasepool {
    [NSApplication sharedApplication];
    if (argc < 2) return 2;
    NSBundle* bundle = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
    NSError* error = nil;
    if (![bundle loadAndReturnError:&error]) { NSLog(@"%@", error); return 2; }
    Class cls = NSClassFromString(@"FirstSynthAUViewController");
    if (!cls) cls = NSClassFromString(@"IPlugAUViewController_vFirstSynth");
    AUViewController<AUAudioUnitFactory>* vc = [[cls alloc]
      initWithNibName:nil bundle:bundle];
    if (argc == 2) (void) vc.view;
    __block BOOL completed = NO;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      AudioComponentDescription desc = {'aumu', 'hnve', 'EZAN', 0, 0};
      NSError* initError = nil;
      AUAudioUnit* unit = [vc createAudioUnitWithComponentDescription:desc error:&initError];
      dispatch_async(dispatch_get_main_queue(), ^{
        if (!unit) NSLog(@"%@", initError);
        completed = YES;
      });
    });
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10];
    while (!completed && deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    NSSize size = vc.preferredContentSize;
    if (!completed || size.width != 1110 || size.height != 606) {
      NSLog(@"FAIL: async AU initialization must report 1110x606, got %@", NSStringFromSize(size));
      return 1;
    }
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1387,757)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
    // A DAW embeds the AU controller inside its own controller. Making the AU
    // the window's contentViewController instead lets AppKit pin the window
    // directly to preferredContentSize, which is a different host contract.
    NSViewController* host = [[NSViewController alloc] init];
    host.view = [[NSView alloc] initWithFrame:NSMakeRect(0,0,1387,757)];
    host.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window.contentViewController = host;
    // The host owns the allocated editor space. Its dimensions take priority
    // over the extension's preferred (initial) size, as in a DAW editor panel.
    NSLayoutConstraint* hostWidth = [host.view.widthAnchor constraintEqualToConstant:1387];
    NSLayoutConstraint* hostHeight = [host.view.heightAnchor constraintEqualToConstant:757];
    [NSLayoutConstraint activateConstraints:@[hostWidth,hostHeight]];
    [host addChildViewController:vc];
    [vc.view setFrame:host.view.bounds];
    [host.view addSubview:vc.view];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    [window setContentSize:NSMakeSize(1387,757)];
    [vc.view setFrameSize:NSMakeSize(1387,757)];
    [vc viewDidLayout];
    WKWebView* web = nil;
    for (int i = 0; !web && i < 60; ++i) { Pump(0.05); web = FindWebView(vc.view); }
    if (!web) { NSLog(@"FAIL: no WKWebView in the AUv3 view"); return 1; }
    BOOL ok = PollUntil(web,
      @"(()=>{const main=document.querySelector('main');const result={width:innerWidth,height:innerHeight,zoom:main.style.zoom,zoomHidden:document.getElementById('uiScaleSelect').hidden,pages:[0,1,2,3].map(i=>{SetPage(i);return {w:main.getBoundingClientRect().width,h:main.getBoundingClientRect().height};})};SetPage(0);return result;})()",
      @"Full-size UI", ^BOOL(id result) {
        BOOL fits = [result[@"zoom"] doubleValue] == 1 && [result[@"zoomHidden"] boolValue]
          && fabs([result[@"width"] doubleValue] - 1387) < 2;
        for (NSDictionary* page in result[@"pages"])
          fits = fits && [page[@"w"] doubleValue] <= [result[@"width"] doubleValue] + 1 && [page[@"h"] doubleValue] <= [result[@"height"] doubleValue] + 1;
        return fits;
      });
    if (!ok) { NSLog(@"FAIL: full-size UI must fit every page at 100%%"); return 1; }
    for (int i=0; i<6; ++i) {
      hostWidth.constant = i%2 ? 1110 : 900;
      hostHeight.constant = i%2 ? 606 : 491;
      [window setFrame:[window frameRectForContentRect:NSMakeRect(0,0,i%2 ? 1110 : 900,i%2 ? 606 : 491)] display:YES];
      [window.contentView layoutSubtreeIfNeeded];
      double expectedWidth = i%2 ? 1110 : 900;
      ok = PollUntil(web, @"({viewport:innerWidth,content:document.querySelector('main').getBoundingClientRect().width})",
        [NSString stringWithFormat:@"Resize %d to %g", i, expectedWidth], ^BOOL(id result) {
          return fabs([result[@"viewport"] doubleValue] - expectedWidth) < 2
            && fabs([result[@"content"] doubleValue] - expectedWidth) < 2;
        });
      if (!ok) { NSLog(@"FAIL: host resize to %g must resize content", expectedWidth); return 1; }
    }
    ok = PollUntil(web, @"({viewport:innerWidth,width:document.querySelector('main').getBoundingClientRect().width,zoom:new DOMMatrix(getComputedStyle(document.querySelector('main')).transform).a})",
      @"Scaled UI", ^BOOL(id result) {
        return fabs([result[@"viewport"] doubleValue] - [result[@"width"] doubleValue]) < 2
          && [result[@"zoom"] doubleValue] > 0.79 && [result[@"zoom"] doubleValue] < 0.81;
      });
    if (!ok) { NSLog(@"FAIL: content must scale with host window"); return 1; }
    [window orderOut:nil];
    NSLog(@"PASS: async initial size and repeated host layout");
    return 0;
  }
}
