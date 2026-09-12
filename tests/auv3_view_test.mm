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
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2]];
    WKWebView* web = FindWebView(vc.view);
    __block BOOL measured = NO;
    __block BOOL fits = NO;
    [web evaluateJavaScript:@"(()=>{const main=document.querySelector('main');const result={width:innerWidth,height:innerHeight,zoom:main.style.zoom,zoomHidden:document.getElementById('uiScaleSelect').hidden,pages:[0,1,2,3].map(i=>{SetPage(i);return {w:main.getBoundingClientRect().width,h:main.getBoundingClientRect().height};})};SetPage(0);return result;})()"
      completionHandler:^(id result, NSError* error) {
        fits = !error && [result[@"zoom"] doubleValue] == 1 && [result[@"zoomHidden"] boolValue];
        for (NSDictionary* page in result[@"pages"])
          fits = fits && [page[@"w"] doubleValue] <= [result[@"width"] doubleValue] + 1 && [page[@"h"] doubleValue] <= [result[@"height"] doubleValue] + 1;
        NSLog(@"Full-size UI: %@ %@", result, error);
        measured = YES;
      }];
    deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (!measured && deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    if (!measured || !fits) { NSLog(@"FAIL: full-size UI must fit every page at 100%%"); return 1; }
    for (int i=0; i<6; ++i) {
      hostWidth.constant = i%2 ? 1110 : 900;
      hostHeight.constant = i%2 ? 606 : 491;
      [window setFrame:[window frameRectForContentRect:NSMakeRect(0,0,i%2 ? 1110 : 900,i%2 ? 606 : 491)] display:YES];
      [window.contentView layoutSubtreeIfNeeded];
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
      measured = NO;
      double expectedWidth = i%2 ? 1110 : 900;
      [web evaluateJavaScript:@"({viewport:innerWidth,content:document.querySelector('main').getBoundingClientRect().width})"
        completionHandler:^(id result, NSError* error) {
          fits = !error && fabs([result[@"viewport"] doubleValue] - expectedWidth) < 2
            && fabs([result[@"content"] doubleValue] - expectedWidth) < 2;
          measured = YES;
        }];
      deadline = [NSDate dateWithTimeIntervalSinceNow:5];
      while (!measured && deadline.timeIntervalSinceNow > 0)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
      if (!measured || !fits) { NSLog(@"FAIL: host resize to %g must resize content", expectedWidth); return 1; }
    }
    measured = NO;
    fits = NO;
    [web evaluateJavaScript:@"({viewport:innerWidth,width:document.querySelector('main').getBoundingClientRect().width,zoom:new DOMMatrix(getComputedStyle(document.querySelector('main')).transform).a})"
      completionHandler:^(id result, NSError* error) {
        fits = !error && fabs([result[@"viewport"] doubleValue] - [result[@"width"] doubleValue]) < 2
          && [result[@"zoom"] doubleValue] > 0.79 && [result[@"zoom"] doubleValue] < 0.81;
        NSLog(@"Scaled UI: %@ %@", result, error);
        measured = YES;
      }];
    deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (!measured && deadline.timeIntervalSinceNow > 0)
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    if (!fits) { NSLog(@"FAIL: content must scale with host window"); return 1; }
    [window orderOut:nil];
    NSLog(@"PASS: async initial size and repeated host layout");
    return 0;
  }
}
