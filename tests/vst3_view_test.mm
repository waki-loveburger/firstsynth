#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/gui/iplugview.h"
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;
class Frame : public IPlugFrame {
public:
  NSView* parent;
  explicit Frame(NSView* p) : parent(p) {}
  tresult PLUGIN_API queryInterface(const TUID, void** obj) override { *obj=nullptr; return kNoInterface; }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
    [parent.window setContentSize:NSMakeSize(size->getWidth(),size->getHeight())];
    return view->onSize(size);
  }
};
static WKWebView* FindWeb(NSView* root) {
  if ([root isKindOfClass:WKWebView.class]) return (WKWebView*)root;
  for (NSView* child in root.subviews) if (auto web = FindWeb(child)) return web;
  return nil;
}
static void Pump(double seconds) {
  NSDate* end = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while (end.timeIntervalSinceNow > 0)
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
}
int main(int argc, const char** argv) {
  @autoreleasepool {
    if (argc != 2) return 2;
    [NSApplication sharedApplication];
    HostApplication host;
    PluginContextFactory::instance().setPluginContext(&host);
    std::string error;
    auto module = VST3::Hosting::Module::create(argv[1], error);
    if (!module) { NSLog(@"Load failed: %s", error.c_str()); return 2; }
    for (auto& info : module->getFactory().classInfos()) {
      if (info.category() != kVstAudioEffectClass) continue;
      PlugProvider provider(module->getFactory(), info, true);
      if (!provider.initialize()) return 2;
      auto controller = provider.getControllerPtr();
      auto view = owned(controller->createView(ViewType::kEditor));
      if (!view || view->canResize() != kResultTrue) return 1;
      ViewRect requested(0,0,900,900);
      view->checkSizeConstraint(&requested);
      if (requested.getWidth() != 900 || requested.getHeight() != 491) {
        NSLog(@"FAIL: VST3 resize must preserve 1387:757 aspect ratio"); return 1;
      }
      NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1110,606)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      [NSApp activateIgnoringOtherApps:YES];
      [window makeKeyAndOrderFront:nil];
      Frame frame(window.contentView);
      view->setFrame(&frame);
      if (view->attached((__bridge void*)window.contentView, kPlatformTypeNSView) != kResultTrue) return 1;
      Pump(3);
      for (int width : {1110, 900, 1387, 1110}) {
        ViewRect size(0,0,width,static_cast<int>(std::lround(width*757.0/1387)));
        view->checkSizeConstraint(&size);
        [window setContentSize:NSMakeSize(size.getWidth(),size.getHeight())];
        view->onSize(&size);
        Pump(0.5);
        WKWebView* web = FindWeb(window.contentView);
        if (!web) return 1;
        __block bool done = false, fits = false;
        [web evaluateJavaScript:@"(()=>{let m=document.querySelector('main');let r={w:innerWidth,h:innerHeight,pages:[0,1,2,3].map(i=>{SetPage(i);return {w:m.getBoundingClientRect().width,h:m.getBoundingClientRect().height};})};SetPage(0);return r;})()"
          completionHandler:^(id result, NSError* error) {
            fits = !error && fabs([result[@"w"] doubleValue]-size.getWidth()) < 2;
            for (NSDictionary* page in result[@"pages"])
              fits = fits && fabs([page[@"w"] doubleValue]-size.getWidth()) < 2
                && [page[@"h"] doubleValue] <= size.getHeight()+2;
            NSLog(@"VST3 %dx%d: %@ %@",size.getWidth(),size.getHeight(),result,error);
            done = true;
          }];
        for (int i=0; !done && i<500; ++i) Pump(0.01);
        if (!done || !fits) { NSLog(@"FAIL: host resizing must fit all pages"); return 1; }
      }
      view->removed();
      view->setFrame(nullptr);
      NSLog(@"PASS: VST3 editor attaches, scales all pages, and closes");
      return 0;
    }
    return 2;
  }
}
