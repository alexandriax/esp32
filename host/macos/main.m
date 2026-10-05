#import <Cocoa/Cocoa.h>
#import "VirtualDisplay.h"
#import "DisplayCapture.h"
#import "USBTransport.h"
#import "NetworkStore.h"

// Compact reusable connection/editor form. Listing saved names does not read passwords.
@interface MossNetworkForm : NSView
@property(nonatomic, strong) NSPopUpButton *picker;
@property(nonatomic, strong) NSTextField *name;
@property(nonatomic, strong) NSSecureTextField *password;
@property(nonatomic, strong) NSButton *remember;
@property(nonatomic, strong) NSTextField *errorLabel;
@property(nonatomic, copy) NSString *savedName;
- (instancetype)initWithNames:(NSArray<NSString *> *)names preferred:(NSString *)preferred
                       editing:(NSString *)editing saveOnly:(BOOL)saveOnly;
@end
@implementation MossNetworkForm
- (instancetype)initWithNames:(NSArray<NSString *> *)names preferred:(NSString *)preferred
                       editing:(NSString *)editing saveOnly:(BOOL)saveOnly {
  if ((self = [super initWithFrame:NSMakeRect(0, 0, 390, saveOnly ? 230 : 295)])) {
    CGFloat y = self.bounds.size.height - 22;
    if (!saveOnly) {
      NSTextField *label = [NSTextField labelWithString:@"Saved network"];
      label.frame = NSMakeRect(0, y, 390, 20); [self addSubview:label]; y -= 31;
      _picker = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, y, 390, 27) pullsDown:NO];
      [_picker addItemWithTitle:@"New network…"];
      for (NSString *name in names) [_picker addItemWithTitle:name];
      _picker.target = self; _picker.action = @selector(selectSaved:);
      [self addSubview:_picker]; y -= 30;
    }
    NSTextField *nameLabel = [NSTextField labelWithString:@"Wi-Fi network name"];
    nameLabel.frame = NSMakeRect(0, y, 390, 20); [self addSubview:nameLabel]; y -= 29;
    _name = [[NSTextField alloc] initWithFrame:NSMakeRect(0, y, 390, 26)];
    _name.placeholderString = @"Exact network name (SSID)";
    _name.accessibilityLabel = @"Wi-Fi network name";
    [self addSubview:_name]; y -= 30;
    NSTextField *passwordLabel = [NSTextField labelWithString:@"Password"];
    passwordLabel.frame = NSMakeRect(0, y, 390, 20); [self addSubview:passwordLabel]; y -= 29;
    _password = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, y, 390, 26)];
    _password.placeholderString = @"Blank for an open network";
    _password.accessibilityLabel = @"Wi-Fi password";
    [self addSubview:_password]; y -= 32;
    _remember = [NSButton checkboxWithTitle:saveOnly ? @"Use this network automatically" : @"Remember network in Keychain and use automatically"
                                    target:nil action:NULL];
    _remember.frame = NSMakeRect(0, y, 390, 22);
    _remember.state = NSControlStateValueOn;
    [self addSubview:_remember];
    _errorLabel = [NSTextField wrappingLabelWithString:@""];
    _errorLabel.textColor = NSColor.systemRedColor;
    _errorLabel.font = [NSFont systemFontOfSize:11];
    _errorLabel.frame = NSMakeRect(0, 0, 390, 52);
    [self addSubview:_errorLabel];
    _picker.nextKeyView = _name; _name.nextKeyView = _password; _password.nextKeyView = _remember;
    if (editing) {
      _name.stringValue = editing; _name.editable = NO;
      _password.placeholderString = @"Enter replacement password; blank for open network";
      _remember.state = [editing isEqualToString:preferred] ? NSControlStateValueOn : NSControlStateValueOff;
    } else if (!saveOnly && [names containsObject:preferred]) {
      [_picker selectItemWithTitle:preferred]; [self selectSaved:nil];
    }
  }
  return self;
}
- (void)selectSaved:(id)sender {
  (void)sender;
  self.savedName = _picker.indexOfSelectedItem > 0 ? _picker.titleOfSelectedItem : nil;
  _name.stringValue = self.savedName ?: @"";
  _password.stringValue = @"";
  _password.placeholderString = self.savedName ? @"Leave blank to use saved password" : @"Blank for an open network";
  _errorLabel.stringValue = @"";
}
@end

@interface MossApp : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate>
@end
@implementation MossApp {
  NSStatusItem *_item;
  NSMenuItem *_status, *_disconnect;
  NSMenuItem *_desktop800, *_desktop960, *_sharp, *_fast;
  NSMenuItem *_adaptive, *_lossless;
  BOOL _adaptivePreference;
  MossUSBTransport *_usb;
  MossVirtualDisplay *_display;
  MossDisplayCapture *_capture;
  uint64_t _session;
  BOOL _quitting, _networkDialogOpen, _connectingSavedNetwork, _skipSavedNetworkOnce;
  uint64_t _wifiSetupPendingSession;
  NSString *_networkSetupError;
  MossNetworkStore *_networks;
  NSAlert *_networkDialog;
  uint64_t _wifiDialogSession;
  NSWindow *_networkWindow;
  NSTableView *_networkTable;
  NSTextField *_networkSummary;
  NSButton *_updateNetwork, *_forgetNetwork, *_autoNetwork;
  NSArray<NSString *> *_networkNames;
  NSString *_preferredNetwork;
  NSMenuItem *_audioChoice, *_audioVolumeItem, *_audioVolumeCurrent;
  NSMenu *_audioVolumeMenu;
  BOOL _audioPreference;
  unsigned _desktopPreference, _pixelPreference;
}
- (void)setStatus:(NSString *)message {
  _status.title = message;
  _item.button.toolTip = message;
  NSLog(@"%@", message);
}
- (NSMenuItem *)menuItem:(NSString *)title action:(SEL)action {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
  item.target = self;
  return item;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  _item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  _item.button.title = @"Moss";
  NSMenu *menu = [[NSMenu alloc] init];
  menu.autoenablesItems = NO;
  _status = [self menuItem:@"Waiting for Moss" action:NULL];
  _status.enabled = NO;
  [menu addItem:_status];
  [menu addItem:[NSMenuItem separatorItem]];
  _disconnect = [self menuItem:@"Disconnect display" action:@selector(disconnect:)];
  _disconnect.enabled = NO;
  [menu addItem:_disconnect];
  const NSInteger desktop = [NSUserDefaults.standardUserDefaults integerForKey:@"desktopSize"];
  const NSInteger pixels = [NSUserDefaults.standardUserDefaults integerForKey:@"pixelSize"];
  _desktopPreference = desktop == 960 ? 960 : 800;
  _pixelPreference = pixels == 240 ? 240 : 480;
  id compression = [NSUserDefaults.standardUserDefaults objectForKey:@"adaptiveCompression"];
  _adaptivePreference = compression ? [compression boolValue] : YES;
  NSMenuItem *desktopItem = [self menuItem:@"Desktop size (next connection)" action:NULL];
  NSMenu *desktopMenu = [[NSMenu alloc] init];
  desktopMenu.autoenablesItems = NO;
  _desktop800 = [self menuItem:@"800 × 800 · larger interface" action:@selector(selectDesktopSize:)];
  _desktop800.tag = 800;
  _desktop960 = [self menuItem:@"960 × 960 · more workspace" action:@selector(selectDesktopSize:)];
  _desktop960.tag = 960;
  [desktopMenu addItem:_desktop800]; [desktopMenu addItem:_desktop960];
  desktopItem.submenu = desktopMenu;
  [menu addItem:desktopItem];
  NSMenuItem *qualityItem = [self menuItem:@"Image quality" action:NULL];
  NSMenu *qualityMenu = [[NSMenu alloc] init];
  qualityMenu.autoenablesItems = NO;
  _sharp = [self menuItem:@"Sharp · 480 × 480" action:@selector(selectTransferSize:)];
  _sharp.tag = 480;
  _fast = [self menuItem:@"Fast · 240 × 240, enlarged" action:@selector(selectTransferSize:)];
  _fast.tag = 240;
  _fast.toolTip = @"Sends one-quarter as many pixels; requires firmware v1.9 or later.";
  [qualityMenu addItem:_sharp]; [qualityMenu addItem:_fast];
  qualityItem.submenu = qualityMenu;
  [menu addItem:qualityItem];
  NSMenuItem *compressionItem = [self menuItem:@"Compression" action:NULL];
  NSMenu *compressionMenu = [[NSMenu alloc] init];
  compressionMenu.autoenablesItems = NO;
  _adaptive = [self menuItem:@"Adaptive · faster motion" action:@selector(selectCompression:)];
  _adaptive.tag = 1;
  _adaptive.toolTip = @"Fast Wi-Fi mode can reduce image detail during motion, then restore an exact image when still.";
  _lossless = [self menuItem:@"Lossless · exact colors" action:@selector(selectCompression:)];
  _lossless.tag = 0;
  [compressionMenu addItem:_adaptive]; [compressionMenu addItem:_lossless];
  compressionItem.submenu = compressionMenu;
  [menu addItem:compressionItem];
  _audioPreference = [NSUserDefaults.standardUserDefaults boolForKey:@"audioEnabled"];
  _audioChoice = [self menuItem:@"Audio on Moss" action:@selector(toggleAudio:)];
  _audioChoice.toolTip = @"Optional Mac audio through Moss’s speaker. Uses mono audio and may reduce display refresh. No microphone capture.";
  [menu addItem:_audioChoice];
  _audioVolumeItem = [self menuItem:@"Moss speaker volume" action:NULL];
  _audioVolumeMenu = [[NSMenu alloc] init];
  _audioVolumeMenu.autoenablesItems = NO;
  _audioVolumeCurrent = [self menuItem:@"Current volume" action:NULL];
  _audioVolumeCurrent.enabled = NO;
  [_audioVolumeMenu addItem:_audioVolumeCurrent];
  for (NSNumber *volume in @[@0, @20, @35, @50, @60]) {
    NSString *title = volume.integerValue ? [NSString stringWithFormat:@"%@%%", volume] : @"Mute";
    NSMenuItem *choice = [self menuItem:title action:@selector(selectAudioVolume:)];
    choice.tag = volume.integerValue; [_audioVolumeMenu addItem:choice];
  }
  _audioVolumeItem.submenu = _audioVolumeMenu; [menu addItem:_audioVolumeItem];
  [menu addItem:[self menuItem:@"Forget wireless pairing…" action:@selector(forgetWirelessPairing:)]];
  [menu addItem:[self menuItem:@"Manage Networks…" action:@selector(manageNetworks:)]];
  [menu addItem:[self menuItem:@"Display Settings…" action:@selector(displaySettings:)]];
  [menu addItem:[self menuItem:@"Screen Recording Permission…" action:@selector(recordingSettings:)]];
  [menu addItem:[NSMenuItem separatorItem]];
  [menu addItem:[self menuItem:@"Quit Moss Display" action:@selector(quit:)]];
  _item.menu = menu;
  _display = [[MossVirtualDisplay alloc] init];
  _capture = [[MossDisplayCapture alloc] init];
  _usb = [[MossUSBTransport alloc] init];
  _usb.allowsLossyCompression = _adaptivePreference;
  _usb.audioEnabled = _audioPreference;
  NSNumber *savedVolume = [NSUserDefaults.standardUserDefaults objectForKey:@"audioVolume"];
  _usb.audioVolume = savedVolume ? savedVolume.unsignedIntegerValue : 35;
  _networks = [[MossNetworkStore alloc] init];
  [self updateChoices];
  __weak MossApp *weakSelf = self;
  _usb.statusHandler = ^(NSString *status) { [weakSelf setStatus:status]; };
  _usb.requestHandler = ^(uint64_t session) { [weakSelf requested:session]; };
  _usb.wifiSetupHandler = ^{ [weakSelf configureWiFi]; };
  _usb.readyHandler = ^(uint64_t session) { [weakSelf ready:session]; };
  _usb.endedHandler = ^(NSString *reason) { [weakSelf removeDisplay:reason]; };
  _usb.capabilitiesHandler = ^(unsigned flags) { (void)flags; [weakSelf updateChoices]; };
  _capture.frameHandler = ^(NSData *pixels) {
    MossApp *app = weakSelf;
    if (!app) return;
    if (CGDisplayIsOnline(app->_display.displayID) != 1 || CGDisplayIsActive(app->_display.displayID) != 1) {
      [app failed:@"Moss desktop was disconnected"];
      return;
    }
    if (CGDisplayIsInMirrorSet(app->_display.displayID) != 0) {
      [app failed:@"Moss was switched to mirroring · reconnect to extend the desktop"];
      return;
    }
    [app->_usb submitFrame:pixels];
  };
  _capture.audioHandler = ^(NSData *pcm) { MossApp *app = weakSelf; if (app) [app->_usb submitAudio:pcm]; };
  _capture.audioFailureHandler = ^(NSString *message) { [weakSelf disableAudioWithMessage:message]; };
  _usb.audioStatusHandler = ^(BOOL active, NSString *message) {
    MossApp *app = weakSelf;
    if (app && !active && [message containsString:@"could not"]) [app disableAudioWithMessage:message];
  };
  _usb.audioVolumeHandler = ^(NSUInteger volume) {
    [NSUserDefaults.standardUserDefaults setInteger:(NSInteger)volume forKey:@"audioVolume"];
    [weakSelf updateChoices];
  };
  _capture.failureHandler = ^(NSString *reason) { [weakSelf failed:reason]; };
  if (![MossVirtualDisplay isSupported]) [self setStatus:@"Virtual display API unavailable on this macOS"];
  else [_usb start];
}
- (void)updateChoices {
  _desktop800.state = _desktopPreference == 800 ? NSControlStateValueOn : NSControlStateValueOff;
  _desktop960.state = _desktopPreference == 960 ? NSControlStateValueOn : NSControlStateValueOff;
  _sharp.state = _pixelPreference == 480 ? NSControlStateValueOn : NSControlStateValueOff;
  _fast.state = _pixelPreference == 240 ? NSControlStateValueOn : NSControlStateValueOff;
  _fast.enabled = !_session || (_usb.capabilities & 2);
  _adaptive.state = _adaptivePreference ? NSControlStateValueOn : NSControlStateValueOff;
  _lossless.state = _adaptivePreference ? NSControlStateValueOff : NSControlStateValueOn;
  _audioChoice.state = _audioPreference ? NSControlStateValueOn : NSControlStateValueOff;
  _audioChoice.enabled = !_session || (_usb.wifiRequested && (_usb.capabilities & 64));
  const NSUInteger volume = _usb.audioVolume;
  _audioVolumeItem.title = [NSString stringWithFormat:@"Moss speaker volume · %lu%%", (unsigned long)volume];
  BOOL preset = NO;
  for (NSMenuItem *choice in _audioVolumeMenu.itemArray) {
    if (choice == _audioVolumeCurrent) continue;
    const BOOL selected = (NSUInteger)choice.tag == volume;
    choice.state = selected ? NSControlStateValueOn : NSControlStateValueOff;
    preset |= selected;
  }
  _audioVolumeCurrent.title = [NSString stringWithFormat:@"Current: %lu%%", (unsigned long)volume];
  _audioVolumeCurrent.hidden = preset;
  _audioVolumeCurrent.state = NSControlStateValueOn;
}
- (void)selectCompression:(NSMenuItem *)item {
  _adaptivePreference = item.tag == 1;
  [NSUserDefaults.standardUserDefaults setBool:_adaptivePreference forKey:@"adaptiveCompression"];
  _usb.allowsLossyCompression = _adaptivePreference;
  [self updateChoices];
}
- (void)selectDesktopSize:(NSMenuItem *)item {
  _desktopPreference = item.tag == 960 ? 960 : 800;
  [NSUserDefaults.standardUserDefaults setInteger:_desktopPreference forKey:@"desktopSize"];
  [self updateChoices];
}
- (void)selectTransferSize:(NSMenuItem *)item {
  const unsigned size = item.tag == 240 ? 240 : 480;
  if (size == 240 && _session && !(_usb.capabilities & 2)) return;
  _pixelPreference = size;
  [NSUserDefaults.standardUserDefaults setInteger:size forKey:@"pixelSize"];
  [self updateChoices];
  if (_session && _display.displayID) {
    [_capture stop];
    _usb.transferSize = size;
    [self startCapture:_session allowCompatibility:NO];
  }
}
- (void)toggleAudio:(id)sender {
  (void)sender; _audioPreference = !_audioPreference;
  [NSUserDefaults.standardUserDefaults setBool:_audioPreference forKey:@"audioEnabled"];
  _usb.audioEnabled = _audioPreference;
  [self updateChoices];
  if (_session && _display.displayID) [self startCapture:_session allowCompatibility:NO];
}
- (void)selectAudioVolume:(NSMenuItem *)sender {
  _usb.audioVolume = (NSUInteger)sender.tag;
  [NSUserDefaults.standardUserDefaults setInteger:(NSInteger)_usb.audioVolume forKey:@"audioVolume"];
  [self updateChoices];
}
- (void)disableAudioWithMessage:(NSString *)message {
  if (!_audioPreference) return;
  _audioPreference = NO; _usb.audioEnabled = NO;
  [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"audioEnabled"];
  [self updateChoices];
  if (_session && _display.displayID) [self startCapture:_session allowCompatibility:NO];
  [self setStatus:[message stringByAppendingString:@" · display continues"]];
}
- (void)forgetWirelessPairing:(id)sender {
  (void)sender;
  NSArray<NSString *> *devices = [_usb pairedDeviceIDs];
  NSAlert *alert = [[NSAlert alloc] init];
  alert.messageText = devices.count ? @"Forget a paired Moss?" : @"No paired Moss devices";
  alert.informativeText = devices.count ? @"This removes the wireless key from this Mac. Connect USB and choose Remote Display to pair again. Moss keeps its saved Wi-Fi network." : @"Connect USB and choose Remote Display to pair once, then use Moss wirelessly.";
  NSPopUpButton *picker = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0,0,320,28) pullsDown:NO];
  for (NSString *device in devices) [picker addItemWithTitle:[@"Moss · " stringByAppendingString:[device substringFromIndex:24]]];
  if (devices.count) { alert.accessoryView = picker; [alert addButtonWithTitle:@"Forget"]; [alert addButtonWithTitle:@"Cancel"]; }
  else [alert addButtonWithTitle:@"OK"];
  [NSApp activateIgnoringOtherApps:YES];
  if ([alert runModal] == NSAlertFirstButtonReturn && devices.count) {
    NSError *error = nil;
    if (![_usb forgetPairedDevice:devices[picker.indexOfSelectedItem] error:&error]) [self setStatus:error.localizedDescription];
  }
}
- (NSDictionary<NSString *, id> *)networkDetailsForEditing:(NSString *)editing saveOnly:(BOOL)saveOnly {
  if (_networkDialogOpen) return nil;
  _networkDialogOpen = YES;
  NSError *loadError = nil;
  NSArray *names = [_networks networkNames:&loadError] ?: @[];
  NSString *preferred = [_networks preferredNetwork:nil];
  MossNetworkForm *form = [[MossNetworkForm alloc] initWithNames:names preferred:preferred editing:editing saveOnly:saveOnly];
  if (loadError) form.errorLabel.stringValue = loadError.localizedDescription;
  else if (!saveOnly && _networkSetupError) form.errorLabel.stringValue = _networkSetupError;
  if (!saveOnly) _networkSetupError = nil;
  NSAlert *dialog = [[NSAlert alloc] init]; _networkDialog = dialog;
  dialog.messageText = saveOnly ? (editing ? @"Update Wi-Fi password" : @"Save a Wi-Fi network") : @"Connect Moss to Wi-Fi";
  dialog.informativeText = saveOnly ? @"Saved passwords stay in this Mac’s Keychain. Changes apply the next time Moss connects." :
      @"Use a 2.4 GHz network reachable from this Mac. Moss remembers this network for wireless use. Remember below also saves it in this Mac’s Keychain. USB is only needed for pairing/setup.";
  [dialog addButtonWithTitle:saveOnly ? @"Save" : @"Connect"];
  [dialog addButtonWithTitle:@"Cancel"];
  dialog.accessoryView = form;
  form.remember.nextKeyView = dialog.buttons.firstObject;
  [NSApp activateIgnoringOtherApps:YES];
  [dialog.window setInitialFirstResponder:editing ? form.password : form.name];
  NSDictionary *result = nil;
  while (!_quitting && [dialog runModal] == NSAlertFirstButtonReturn) {
    NSString *name = [form.name.stringValue copy], *password = [form.password.stringValue copy];
    NSError *error = nil;
    if (!saveOnly && !password.length && [name isEqualToString:form.savedName])
      password = [_networks passwordForNetwork:name error:&error];
    if (error) { form.errorLabel.stringValue = error.localizedDescription; continue; }
    if (!MossNetworkCredentialsValid(name, password)) {
      form.errorLabel.stringValue = @"Enter a 1–32 byte network name and an 8–63 byte password, or leave the password blank for an open network.";
      continue;
    }
    BOOL remember = form.remember.state == NSControlStateValueOn;
    if (saveOnly || remember) {
      if (![_networks saveNetwork:name password:password error:&error] ||
          (remember && ![_networks setPreferredNetwork:name error:&error]) ||
          (!remember && [name isEqualToString:preferred] && ![_networks setPreferredNetwork:nil error:&error])) {
        form.errorLabel.stringValue = [error.localizedDescription stringByAppendingString:saveOnly ? @"" : @" Uncheck Remember to connect once."];
        continue;
      }
    }
    result = @{ @"ssid":name, @"password":password };
    break;
  }
  form.password.stringValue = @""; form.name.stringValue = @"";
  [dialog.window orderOut:nil];
  _networkDialog = nil; _networkDialogOpen = NO;
  if (_wifiSetupPendingSession) {
    const uint64_t pending = _wifiSetupPendingSession;
    _wifiSetupPendingSession = 0;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (self->_usb.session == pending && self->_usb.wifiRequested && !self->_quitting) [self configureWiFi];
    });
  }
  [self reloadNetworks];
  return result;
}
- (void)configureWiFi {
  const uint64_t session = _usb.session;
  if (!session || !_usb.wifiRequested) return;
  if (_networkDialogOpen) { _wifiSetupPendingSession = session; return; }
  NSError *error = nil;
  NSString *preferred = [_networks preferredNetwork:&error];
  if (!_skipSavedNetworkOnce && preferred) {
    NSString *password = [_networks passwordForNetwork:preferred error:&error];
    if (password != nil && MossNetworkCredentialsValid(preferred, password)) {
      _connectingSavedNetwork = YES;
      [_usb configureWiFiSSID:preferred password:password];
      return;
    }
  }
  _skipSavedNetworkOnce = NO;
  if (error) _networkSetupError = error.localizedDescription;
  _wifiDialogSession = session;
  NSDictionary *details = [self networkDetailsForEditing:nil saveOnly:NO];
  _wifiDialogSession = 0;
  if (_usb.session == session && !_quitting) {
    if (details) [_usb configureWiFiSSID:details[@"ssid"] password:details[@"password"]];
    else [_usb endSession:@"Wi-Fi setup cancelled"];
  }
}
- (NSString *)selectedNetwork {
  NSInteger row = _networkTable.selectedRow;
  return row >= 0 && row < (NSInteger)_networkNames.count ? _networkNames[(NSUInteger)row] : nil;
}
- (void)reloadNetworks {
  if (!_networkWindow) return;
  NSString *selected = [self selectedNetwork];
  NSError *error = nil;
  _networkNames = [_networks networkNames:&error] ?: @[];
  _preferredNetwork = [_networks preferredNetwork:&error];
  [_networkTable reloadData];
  NSUInteger index = selected ? [_networkNames indexOfObject:selected] : NSNotFound;
  if (index == NSNotFound && _networkNames.count) index = 0;
  if (index != NSNotFound) [_networkTable selectRowIndexes:[NSIndexSet indexSetWithIndex:index] byExtendingSelection:NO];
  _networkSummary.stringValue = error ? error.localizedDescription :
      (_preferredNetwork ? [@"Auto-connect: " stringByAppendingString:_preferredNetwork] : @"Auto-connect is off. Choose a network when Moss requests Wi-Fi.");
  [self updateNetworkSelection];
}
- (void)updateNetworkSelection {
  NSString *name = [self selectedNetwork];
  _updateNetwork.enabled = _forgetNetwork.enabled = _autoNetwork.enabled = name != nil;
  _autoNetwork.state = [name isEqualToString:_preferredNetwork] ? NSControlStateValueOn : NSControlStateValueOff;
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
  (void)tableView; return (NSInteger)_networkNames.count;
}
- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
  (void)tableView; (void)column; return _networkNames[(NSUInteger)row];
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification { (void)notification; [self updateNetworkSelection]; }
- (NSButton *)networkButton:(NSString *)title frame:(NSRect)frame action:(SEL)action {
  NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
  button.frame = frame; [_networkWindow.contentView addSubview:button]; return button;
}
- (void)manageNetworks:(id)sender {
  (void)sender;
  if (!_networkWindow) {
    _networkWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 540, 410)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    _networkWindow.title = @"Moss Display · Saved Networks"; _networkWindow.releasedWhenClosed = NO;
    NSTextField *intro = [NSTextField labelWithString:@"Passwords are stored locally in your Mac’s Keychain."];
    intro.frame = NSMakeRect(22, 371, 496, 20); [_networkWindow.contentView addSubview:intro];
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(22, 145, 496, 212)];
    scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder;
    _networkTable = [[NSTableView alloc] initWithFrame:scroll.bounds];
    NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"network"];
    column.title = @"Network name"; column.width = 474; [_networkTable addTableColumn:column];
    _networkTable.rowHeight = 28; _networkTable.allowsMultipleSelection = NO;
    _networkTable.delegate = self; _networkTable.dataSource = self;
    _networkTable.doubleAction = @selector(updateNetwork:); _networkTable.target = self;
    scroll.documentView = _networkTable; [_networkWindow.contentView addSubview:scroll];
    NSButton *add = [self networkButton:@"Add Network…" frame:NSMakeRect(20, 102, 136, 30) action:@selector(addNetwork:)];
    _updateNetwork = [self networkButton:@"Update Password…" frame:NSMakeRect(163, 102, 178, 30) action:@selector(updateNetwork:)];
    _forgetNetwork = [self networkButton:@"Forget Network" frame:NSMakeRect(347, 102, 173, 30) action:@selector(forgetNetwork:)];
    _autoNetwork = [NSButton checkboxWithTitle:@"Auto-connect to the selected network" target:self action:@selector(selectAutomaticNetwork:)];
    _autoNetwork.frame = NSMakeRect(22, 68, 496, 24); [_networkWindow.contentView addSubview:_autoNetwork];
    _networkSummary = [NSTextField wrappingLabelWithString:@""];
    _networkSummary.frame = NSMakeRect(22, 15, 496, 42); _networkSummary.textColor = NSColor.secondaryLabelColor;
    [_networkWindow.contentView addSubview:_networkSummary];
    _networkTable.nextKeyView = add; add.nextKeyView = _updateNetwork; _updateNetwork.nextKeyView = _forgetNetwork;
    _forgetNetwork.nextKeyView = _autoNetwork; _autoNetwork.nextKeyView = _networkTable;
    [_networkWindow center];
  }
  [self reloadNetworks];
  [NSApp activateIgnoringOtherApps:YES]; [_networkWindow makeKeyAndOrderFront:nil];
}
- (void)addNetwork:(id)sender { (void)sender; [self networkDetailsForEditing:nil saveOnly:YES]; }
- (void)updateNetwork:(id)sender {
  (void)sender; NSString *name = [self selectedNetwork];
  if (name) [self networkDetailsForEditing:name saveOnly:YES];
}
- (void)forgetNetwork:(id)sender {
  (void)sender; NSString *name = [self selectedNetwork]; if (!name) return;
  NSAlert *alert = [[NSAlert alloc] init]; alert.messageText = @"Forget this saved network?";
  alert.informativeText = @"Moss Display will remove its saved password from this Mac’s Keychain. Your Mac’s own Wi-Fi settings stay unchanged.";
  [alert addButtonWithTitle:@"Forget"]; [alert addButtonWithTitle:@"Cancel"];
  if ([alert runModal] != NSAlertFirstButtonReturn) return;
  NSError *error = nil;
  if (![_networks forgetNetwork:name error:&error]) { _networkSummary.stringValue = error.localizedDescription; return; }
  [self reloadNetworks];
}
- (void)selectAutomaticNetwork:(id)sender {
  (void)sender; NSString *name = [self selectedNetwork]; if (!name) return;
  NSError *error = nil;
  if (![_networks setPreferredNetwork:_autoNetwork.state == NSControlStateValueOn ? name : nil error:&error]) {
    _networkSummary.stringValue = error.localizedDescription; [self updateNetworkSelection]; return;
  }
  [self reloadNetworks];
}
- (void)requested:(uint64_t)session {
  if (_quitting) return;
  _session = session;
  _disconnect.enabled = YES;
  [self updateChoices];
  if (!CGPreflightScreenCaptureAccess()) {
    [self setStatus:@"Screen Recording permission required"];
    [_usb reportHostStatus:1];
    // The OS owns this approval. Never read/modify TCC settings or automate the
    // dialog. The device must still acknowledge HELLO after permission returns.
    if (!CGRequestScreenCaptureAccess()) {
      [_usb endSession:@"Allow Moss Display in Screen Recording, then select Remote Display on the device again"];
      return;
    }
  }
  if (_usb.session == session && !_quitting) [_usb acceptRequest:session];
}
- (void)ready:(uint64_t)session {
  if (_quitting || session != _session || session != _usb.session) return;
  _connectingSavedNetwork = NO;
  [self setStatus:@"Starting the Moss extended desktop…"];
  _usb.transferSize = _pixelPreference == 240 && (_usb.capabilities & 2) ? 240 : 480;
  [_display createWithDesktopSize:_desktopPreference completion:^(NSError *error) {
    if (self->_session != session || self->_quitting) return;
    if (error) { [self failed:error.localizedDescription]; return; }
    [self startCapture:session allowCompatibility:YES];
  }];
}
- (void)startCapture:(uint64_t)session allowCompatibility:(BOOL)allowCompatibility {
  const unsigned pixelSize = _usb.transferSize;
  _capture.audioEnabled = _audioPreference && _usb.wifiConnected && (_usb.capabilities & 64);
  const BOOL requestedAudio = _capture.audioEnabled;
  [_capture startDisplay:_display.displayID pixelSize:pixelSize completion:^(NSError *captureError) {
    if (self->_session != session || self->_quitting) return;
    if (captureError && allowCompatibility && self->_display.desktopSize == 800 &&
        [captureError.domain isEqualToString:@"MossDisplay"] && captureError.code == 2) {
      [self->_capture stop];
      [self->_display useCompatibilityModeWithCompletion:^(NSError *modeError) {
        if (self->_session != session || self->_quitting) return;
        if (modeError) { [self failed:modeError.localizedDescription]; return; }
        [self startCapture:session allowCompatibility:NO];
      }];
      return;
    }
    if (captureError && requestedAudio && self->_audioPreference) {
      [self disableAudioWithMessage:@"Mac audio capture could not start"];
      return; // Retry the same desktop once with optional audio disabled.
    }
    if (captureError) { [self failed:captureError.localizedDescription]; return; }
    [self setStatus:[NSString stringWithFormat:@"Connected via %@ · %u desktop · %@ pixels",
        self->_usb.wifiConnected ? @"Wi-Fi" : @"USB", self->_display.desktopSize, pixelSize == 240 ? @"240 fast" : @"480 sharp"]];
  }];
}
- (void)removeDisplay:(NSString *)reason {
  if (_connectingSavedNetwork) {
    _skipSavedNetworkOnce = YES; _connectingSavedNetwork = NO;
    _networkSetupError = @"The saved network did not connect. Check its password or choose another network.";
  }
  if (_wifiDialogSession && _networkDialog) {
    [NSApp abortModal]; [_networkDialog.window orderOut:nil];
  }
  const CGDirectDisplayID previous = _display.displayID;
  _session = 0;
  [_capture stop];
  [_display remove];
  if (previous) NSLog(@"Virtual display removed: id=%u online=%u", previous, CGDisplayIsOnline(previous));
  _disconnect.enabled = NO;
  [self updateChoices];
  [self setStatus:reason];
}
- (void)failed:(NSString *)reason {
  [_usb reportHostStatus:2];
  [_usb endSession:reason];
  [self removeDisplay:reason];
}
- (void)disconnect:(id)sender {
  (void)sender;
  [_usb endSession:@"Disconnected · select Remote Display on Moss to reconnect"];
  [self removeDisplay:@"Disconnected · select Remote Display on Moss to reconnect"];
}
- (void)displaySettings:(id)sender {
  (void)sender;
  [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.Displays-Settings.extension"]];
}
- (void)recordingSettings:(id)sender {
  (void)sender;
  // Opening settings is user initiated; all permission toggles remain manual.
  [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}
- (void)quit:(id)sender { (void)sender; [NSApp terminate:nil]; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
  (void)sender;
  if (_quitting) return NSTerminateNow;
  _quitting = YES;
  [_usb endSession:@"Moss Display is quitting"];
  [self removeDisplay:@"Moss Display is quitting"];
  // Allow the framed STOP/RELEASE handshake to drain. Device timeout remains a
  // backstop if the cable disappeared or the companion was force-quit.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    [self->_usb stop];
    [NSApp replyToApplicationShouldTerminate:YES];
  });
  return NSTerminateLater;
}
@end

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    // A read-only smoke check; it creates no display, captures no pixels and
    // does not open serial or trigger a permission prompt.
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
      printf("Virtual display API: %s\n", [MossVirtualDisplay isSupported] ? "available" : "unavailable");
      return [MossVirtualDisplay isSupported] ? 0 : 1;
    }
    NSArray *running = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"org.moss.usb-display"];
    if (running.count > 1) return 0;
    NSApplication *app = [NSApplication sharedApplication];
    MossApp *delegate = [[MossApp alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
