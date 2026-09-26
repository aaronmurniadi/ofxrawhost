#import "Controller.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "ImageIO.h"
#include "OfxHost.h"
#include "ofxParam.h"

#include <cmath>

static const struct {
  NSString *label;
  CFStringRef space;
} kOutputSpaces[] = {
  {@"sRGB", kCGColorSpaceSRGB},
  {@"Display P3", kCGColorSpaceDisplayP3},
  {@"Linear Rec.709", kCGColorSpaceLinearSRGB},
  {@"Linear Rec.2020", kCGColorSpaceLinearITUR_2020},
};

static NSString *NS(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

struct Row {
  Param *p;
  NSArray<NSControl *> *controls;
  NSView *view;
};

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped {
  return YES;
}
@end

@implementation Controller {
  NSWindow *_window;
  NSImageView *_imageView;
  NSStackView *_paramStack;
  NSPopUpButton *_pluginPopup, *_outputPopup;
  NSTextField *_status;
  CIContext *_ciContext;
  NSURL *_imageURL;
  CIImage *_image;
  Pixels _preview;
  int _pw, _ph;
  int _pluginIndex;
  std::unique_ptr<Effect> _instance;
  std::vector<Row> _rows;
  std::map<std::string, bool> _groupOpen;
  dispatch_queue_t _renderQueue;
  NSSavePanel *_savePanel;
  NSInteger _exportFormat;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
  _ciContext = [CIContext contextWithOptions:@{kCIContextWorkingFormat : @(kCIFormatRGBAf)}];
  _renderQueue = dispatch_queue_create("render", DISPATCH_QUEUE_SERIAL);
  _pluginIndex = -1;
  [self buildMenu];
  [self buildWindow];
  __weak Controller *weakSelf = self;
  gOnMessage = ^(NSString *msg) {
    dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf setStatus:msg]; });
  };
  loadPlugins();
  for (auto &pe : gPlugins) [_pluginPopup addItemWithTitle:NS(pe.label)];
  if (gPlugins.empty()) [self setStatus:@"No OFX filter plugins found in /Library/OFX/Plugins or OFX_PLUGIN_PATH"];
  else [self pluginChanged:_pluginPopup];
  NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
  if (args.count > 1 && ![args[1] hasPrefix:@"-"]) [self openURL:[NSURL fileURLWithPath:args[1]]];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
  return YES;
}

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
  [self openURL:urls.firstObject];
}

- (void)buildMenu {
  NSMenu *bar = [NSMenu new];
  NSMenuItem *appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
  appItem.submenu = [NSMenu new];
  [appItem.submenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
  NSMenuItem *fileItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
  fileItem.submenu = [[NSMenu alloc] initWithTitle:@"File"];
  [fileItem.submenu addItemWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
  [fileItem.submenu addItemWithTitle:@"Export…" action:@selector(exportDocument:) keyEquivalent:@"e"];
  NSApp.mainMenu = bar;
}

- (void)buildWindow {
  _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1400, 900)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = @"OFX Raw Host";

  _pluginPopup = [NSPopUpButton new];
  _pluginPopup.target = self;
  _pluginPopup.action = @selector(pluginChanged:);
  _outputPopup = [NSPopUpButton new];
  for (auto &o : kOutputSpaces) [_outputPopup addItemWithTitle:o.label];
  _outputPopup.target = self;
  _outputPopup.action = @selector(scheduleRender);
  _status = [NSTextField labelWithString:@"Open an image with ⌘O. Source is fed to the plugin as scene-linear Rec.2020."];
  _status.lineBreakMode = NSLineBreakByTruncatingTail;
  [_status setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView *top = [NSStackView stackViewWithViews:@[
    [NSButton buttonWithTitle:@"Open…" target:self action:@selector(openDocument:)],
    [NSButton buttonWithTitle:@"Export…" target:self action:@selector(exportDocument:)],
    [NSTextField labelWithString:@"Plugin:"], _pluginPopup, [NSTextField labelWithString:@"Output tag:"], _outputPopup, _status
  ]];

  _imageView = [NSImageView new];
  _imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
  for (NSLayoutConstraintOrientation o : {NSLayoutConstraintOrientationHorizontal, NSLayoutConstraintOrientationVertical}) {
    [_imageView setContentCompressionResistancePriority:1 forOrientation:o];
    [_imageView setContentHuggingPriority:1 forOrientation:o];
  }

  _paramStack = [NSStackView new];
  _paramStack.orientation = NSUserInterfaceLayoutOrientationVertical;
  _paramStack.alignment = NSLayoutAttributeLeading;
  _paramStack.spacing = 4;
  _paramStack.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  _paramStack.translatesAutoresizingMaskIntoConstraints = NO;
  FlippedView *doc = [FlippedView new];
  doc.translatesAutoresizingMaskIntoConstraints = NO;
  [doc addSubview:_paramStack];
  NSScrollView *scroll = [NSScrollView new];
  scroll.hasVerticalScroller = YES;
  scroll.documentView = doc;
  [NSLayoutConstraint activateConstraints:@[
    [_paramStack.topAnchor constraintEqualToAnchor:doc.topAnchor],
    [_paramStack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
    [_paramStack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor],
    [_paramStack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
    [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
    [scroll.widthAnchor constraintGreaterThanOrEqualToConstant:460],
  ]];

  NSSplitView *split = [NSSplitView new];
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  [split addArrangedSubview:_imageView];
  [split addArrangedSubview:scroll];
  [split setHoldingPriority:NSLayoutPriorityDefaultLow - 1 forSubviewAtIndex:0];

  NSStackView *root = [NSStackView stackViewWithViews:@[ top, split ]];
  root.orientation = NSUserInterfaceLayoutOrientationVertical;
  root.alignment = NSLayoutAttributeLeading;
  root.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  root.translatesAutoresizingMaskIntoConstraints = NO;
  [_window.contentView addSubview:root];
  NSView *content = _window.contentView;
  [NSLayoutConstraint activateConstraints:@[
    [root.topAnchor constraintEqualToAnchor:content.topAnchor],
    [root.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    [root.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [root.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [split.widthAnchor constraintEqualToAnchor:root.widthAnchor constant:-16],
    [top.widthAnchor constraintEqualToAnchor:split.widthAnchor],
  ]];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
}

- (void)setStatus:(NSString *)text {
  _status.stringValue = text;
}

// -- plugin instance

- (void)destroyInstance {
  if (!_instance) return;
  ++gLatestGen;
  dispatch_sync(_renderQueue, ^{});
  callAction(gPlugins[_pluginIndex].plugin, kOfxActionDestroyInstance, _instance.get());
  _instance.reset();
}

- (void)pluginChanged:(NSPopUpButton *)sender {
  [self destroyInstance];
  _pluginIndex = (int)sender.indexOfSelectedItem;
  if (_pluginIndex < 0) return;
  _instance = createInstance(gPlugins[_pluginIndex]);
  if (!_instance) [self setStatus:@"Plugin failed to create an instance"];
  if (_instance && _preview) {
    _instance->w = _pw;
    _instance->h = _ph;
  }
  _groupOpen.clear();
  if (_instance) {
    [self applyColorDefaults];
    [self syncOutputTag];
  }
  if (_instance)
    for (auto &p : _instance->params)
      if (p->type == kOfxParamTypeGroup) _groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  [self buildParamUI];
  [self scheduleRender];
}

// -- parameter panel

- (void)buildParamUI {
  for (NSView *v in _paramStack.arrangedSubviews.copy) [v removeFromSuperview];
  _rows.clear();
  if (_instance) [self addParamsWithParent:""];
  [self refreshControls];
}

- (void)addParamsWithParent:(const std::string &)parent {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (p->type == kOfxParamTypeGroup) {
      NSButton *header = [NSButton buttonWithTitle:@"" target:self action:@selector(toggleGroup:)];
      header.bordered = NO;
      header.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
      header.tag = (NSInteger)_rows.size();
      [_paramStack addArrangedSubview:header];
      _rows.push_back({p, @[ header ], header});
      [self addParamsWithParent:p->name];
    } else {
      [self addRowFor:p];
    }
  }
}

- (NSTextField *)numberField {
  NSTextField *f = [NSTextField textFieldWithString:@""];
  f.target = self;
  f.action = @selector(changed:);
  [f.widthAnchor constraintEqualToConstant:64].active = YES;
  return f;
}

- (void)addRowFor:(Param *)p {
  const std::string &t = p->type;
  NSString *label = NS(sprop(p->props, kOfxPropLabel));
  NSMutableArray<NSControl *> *controls = [NSMutableArray array];
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
    double lo = dprop(p->props, kOfxParamPropDisplayMin, 0, dprop(p->props, kOfxParamPropMin, 0, 0));
    double hi = dprop(p->props, kOfxParamPropDisplayMax, 0, dprop(p->props, kOfxParamPropMax, 0, t == kOfxParamTypeInteger ? 100 : 1));
    if (!(std::fabs(lo) < 1e7)) lo = 0;
    if (!(std::fabs(hi) < 1e7) || hi <= lo) hi = lo + (t == kOfxParamTypeInteger ? 100 : 1);
    NSSlider *slider = [NSSlider sliderWithValue:lo minValue:lo maxValue:hi target:self action:@selector(changed:)];
    [slider.widthAnchor constraintGreaterThanOrEqualToConstant:140].active = YES;
    [controls addObjectsFromArray:@[ slider, [self numberField] ]];
  } else if (t == kOfxParamTypeBoolean) {
    [controls addObject:[NSButton checkboxWithTitle:@"" target:self action:@selector(changed:)]];
  } else if (t == kOfxParamTypeChoice) {
    NSPopUpButton *popup = [NSPopUpButton new];
    auto it = p->props.m.find(kOfxParamPropChoiceOption);
    if (it != p->props.m.end())
      for (auto &o : it->second) [popup addItemWithTitle:NS(o.s)];
    popup.target = self;
    popup.action = @selector(changed:);
    [controls addObject:popup];
  } else if (t == kOfxParamTypePushButton) {
    [controls addObject:[NSButton buttonWithTitle:label target:self action:@selector(changed:)]];
    label = @"";
  } else if (t == kOfxParamTypeString) {
    NSTextField *f = [NSTextField textFieldWithString:@""];
    f.editable = sprop(p->props, kOfxParamPropStringMode) != kOfxParamStringIsLabel;
    f.target = self;
    f.action = @selector(changed:);
    [f.widthAnchor constraintGreaterThanOrEqualToConstant:200].active = YES;
    [controls addObject:f];
  } else if (dims(t) > 1) {
    for (int i = 0; i < dims(t); ++i) [controls addObject:[self numberField]];
  } else {
    return;  // custom / parametric params are not supported
  }
  NSTextField *labelField = [NSTextField labelWithString:label];
  labelField.lineBreakMode = NSLineBreakByTruncatingTail;
  labelField.toolTip = NS(sprop(p->props, kOfxParamPropHint));
  [labelField.widthAnchor constraintEqualToConstant:170].active = YES;
  for (NSControl *c in controls) c.tag = (NSInteger)_rows.size();
  NSStackView *row = [NSStackView stackViewWithViews:[@[ labelField ] arrayByAddingObjectsFromArray:controls]];
  [_paramStack addArrangedSubview:row];
  _rows.push_back({p, controls, row});
}

- (bool)ancestorsOpen:(const std::string &)group {
  if (group.empty()) return true;
  Param *g = findParam(_instance.get(), group.c_str());
  return g && _groupOpen[group] && [self ancestorsOpen:sprop(g->props, kOfxParamPropParent)];
}

// No gValueMutex here: hiding a focused field ends editing, which re-enters -changed: and takes the lock.
// Values are only written on the main thread, so main-thread reads need no lock.
- (void)refreshControls {
  for (Row &r : _rows) {
    Param *p = r.p;
    r.view.hidden = dprop(p->props, kOfxParamPropSecret, 0, 0) != 0 || ![self ancestorsOpen:sprop(p->props, kOfxParamPropParent)];
    const bool enabled = dprop(p->props, kOfxParamPropEnabled, 0, 1) != 0;
    for (NSControl *c in r.controls) c.enabled = enabled;
    const std::string &t = p->type;
    if (t == kOfxParamTypeGroup) {
      NSButton *b = (NSButton *)r.controls[0];
      b.title = [NSString stringWithFormat:@"%@ %@", _groupOpen[p->name] ? @"▾" : @"▸", NS(sprop(p->props, kOfxPropLabel))];
    } else if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
      r.controls[0].doubleValue = p->v[0];
      r.controls[1].stringValue = [NSString stringWithFormat:@"%.4g", p->v[0]];
    } else if (t == kOfxParamTypeBoolean) {
      ((NSButton *)r.controls[0]).state = p->v[0] != 0 ? NSControlStateValueOn : NSControlStateValueOff;
    } else if (t == kOfxParamTypeChoice) {
      [(NSPopUpButton *)r.controls[0] selectItemAtIndex:(NSInteger)p->v[0]];
    } else if (t == kOfxParamTypeString) {
      r.controls[0].stringValue = NS(p->s);
    } else if (t != kOfxParamTypePushButton) {
      for (size_t i = 0; i < p->v.size(); ++i) r.controls[i].stringValue = [NSString stringWithFormat:@"%.4g", p->v[i]];
    }
  }
}

- (void)toggleGroup:(NSButton *)sender {
  const std::string &name = _rows[sender.tag].p->name;
  _groupOpen[name] = !_groupOpen[name];
  [self refreshControls];
}

- (void)changed:(NSControl *)sender {
  Row &r = _rows[sender.tag];
  Param *p = r.p;
  const std::string &t = p->type;
  {
    std::lock_guard<std::mutex> lock(gValueMutex);
    if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
      double v = sender.doubleValue;
      p->v[0] = t == kOfxParamTypeInteger ? std::round(v) : v;
    } else if (t == kOfxParamTypeBoolean) {
      p->v[0] = ((NSButton *)sender).state == NSControlStateValueOn;
    } else if (t == kOfxParamTypeChoice) {
      p->v[0] = (double)((NSPopUpButton *)sender).indexOfSelectedItem;
    } else if (t == kOfxParamTypeString) {
      p->s = sender.stringValue.UTF8String;
    } else if (t != kOfxParamTypePushButton) {
      for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = isIntType(t) ? std::round(r.controls[i].doubleValue) : r.controls[i].doubleValue;
    }
  }
  [self notifyChanged:p];
  [self syncOutputTag];
  [self refreshControls];
  [self scheduleRender];
}

- (void)notifyChanged:(Param *)p {
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const double scale[2] = {1, 1};
  propSetString(a, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(a, kOfxPropName, 0, p->name.c_str());
  propSetString(a, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  callAction(plugin, kOfxActionBeginInstanceChanged, _instance.get(), &in);
  callAction(plugin, kOfxActionInstanceChanged, _instance.get(), &in);
  callAction(plugin, kOfxActionEndInstanceChanged, _instance.get(), &in);
}

static const std::vector<Val> &choiceOptions(Param *p) {
  static const std::vector<Val> none;
  auto it = p->props.m.find(kOfxParamPropChoiceOption);
  return it != p->props.m.end() ? it->second : none;
}

// The host always feeds scene-linear Rec.2020; default the plugin's output to web-safe sRGB.
- (void)applyColorDefaults {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice) continue;
    const std::string label = sprop(p->props, kOfxPropLabel);
    const char *want = label == "Input Color Space" ? "Linear Rec.2020" : label == "Output Color Space" ? "sRGB" : nullptr;
    const auto &options = choiceOptions(p);
    for (size_t i = 0; want && i < options.size(); ++i) {
      if (options[i].s != want) continue;
      {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[0] = (double)i;
      }
      [self notifyChanged:p];
      break;
    }
  }
}

// Tags display/export with the plugin's visible output color space when the host knows it.
- (void)syncOutputTag {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice || sprop(p->props, kOfxPropLabel) != "Output Color Space" ||
        dprop(p->props, kOfxParamPropSecret, 0, 0) != 0)
      continue;
    const auto &options = choiceOptions(p);
    const size_t index = (size_t)p->v[0];
    if (index < options.size()) [_outputPopup selectItemWithTitle:NS(options[index].s)];
    if (_outputPopup.indexOfSelectedItem < 0) [_outputPopup selectItemAtIndex:0];
  }
}

// -- rendering

- (CFStringRef)outputSpace {
  return kOutputSpaces[std::max<NSInteger>(0, _outputPopup.indexOfSelectedItem)].space;
}

- (void)scheduleRender {
  if (!_instance || !_preview) return;
  const int gen = ++gLatestGen;
  Effect *effect = _instance.get();
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  Pixels src = _preview;
  const int w = _pw, h = _ph;
  CFStringRef space = [self outputSpace];
  [self setStatus:@"Rendering…"];
  dispatch_async(_renderQueue, ^{
    if (gen != gLatestGen) return;
    std::vector<float> out(src->size());
    const OfxStatus st = renderEffect(plugin, effect, src->data(), out.data(), w, h, gen);
    if (gen != gLatestGen) return;
    CGImageRef cg = st == kOfxStatOK ? makeCGImage(out, w, h, space) : nullptr;
    NSImage *img = cg ? [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(w, h)] : nil;
    CGImageRelease(cg);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (gen != gLatestGen) return;
      if (img) self->_imageView.image = img;
      [self setStatus:img ? [NSString stringWithFormat:@"%d×%d preview", w, h]
                          : [NSString stringWithFormat:@"Render failed (OFX status %d)", st]];
    });
  });
}

// -- documents

- (void)openDocument:(id)sender {
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  panel.allowedContentTypes = @[ UTTypeImage ];
  if ([panel runModal] == NSModalResponseOK) [self openURL:panel.URL];
}

- (void)openURL:(NSURL *)url {
  CIImage *img = url ? loadImage(url) : nil;
  if (!img) {
    [self setStatus:[NSString stringWithFormat:@"Could not decode %@", url.lastPathComponent]];
    return;
  }
  _imageURL = url;
  _image = img;
  _preview = renderSource(_ciContext, img, 1600, _pw, _ph);
  if (_instance) {
    _instance->w = _pw;
    _instance->h = _ph;
  }
  _window.title = url.lastPathComponent;
  [self scheduleRender];
}

- (void)exportFormatChanged:(NSPopUpButton *)sender {
  _exportFormat = std::max<NSInteger>(0, sender.indexOfSelectedItem);
  UTType *types[] = {UTTypeTIFF, UTTypePNG, UTTypeJPEG, [UTType typeWithIdentifier:@"com.ilm.openexr-image"]};
  _savePanel.allowedContentTypes = @[ types[_exportFormat] ];
}

- (void)exportDocument:(id)sender {
  if (!_image || !_instance) return;
  NSSavePanel *panel = [NSSavePanel savePanel];
  _savePanel = panel;
  NSPopUpButton *format = [NSPopUpButton new];
  [format addItemsWithTitles:@[ @"TIFF (16-bit)", @"PNG (16-bit)", @"JPEG", @"OpenEXR (float)" ]];
  [format selectItemAtIndex:_exportFormat];
  format.target = self;
  format.action = @selector(exportFormatChanged:);
  NSStackView *accessory = [NSStackView stackViewWithViews:@[ [NSTextField labelWithString:@"Format:"], format ]];
  accessory.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  panel.accessoryView = accessory;
  panel.nameFieldStringValue = _imageURL.lastPathComponent.stringByDeletingPathExtension;
  [self exportFormatChanged:format];
  const NSModalResponse response = [panel runModal];
  _savePanel = nil;
  if (response != NSModalResponseOK) return;
  NSURL *url = panel.URL;
  CIImage *image = _image;
  CIContext *ciContext = _ciContext;
  Effect *effect = _instance.get();
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  CFStringRef space = [self outputSpace];
  const int pw = _pw, ph = _ph;
  [self setStatus:@"Exporting full resolution…"];
  ++gLatestGen;
  dispatch_async(_renderQueue, ^{
    int w = 0, h = 0;
    Pixels src = renderSource(ciContext, image, 0, w, h);
    std::vector<float> out(src->size());
    OfxStatus st = renderEffect(plugin, effect, src->data(), out.data(), w, h, 0);
    effect->w = pw;
    effect->h = ph;
    bool ok = false;
    if (st == kOfxStatOK) {
      CGImageRef cg = makeCGImage(out, w, h, space);
      ok = writeImage(cg, url);
      CGImageRelease(cg);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      [self setStatus:ok ? [NSString stringWithFormat:@"Exported %@ (%d×%d)", url.lastPathComponent, w, h]
                         : [NSString stringWithFormat:@"Export failed (OFX status %d)", st]];
    });
  });
}

@end

