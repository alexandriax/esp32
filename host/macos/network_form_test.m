#import <Cocoa/Cocoa.h>
#include <assert.h>

@interface MossNetworkForm : NSView
@property(nonatomic, strong) NSPopUpButton *picker;
@property(nonatomic, strong) NSTextField *name;
@property(nonatomic, strong) NSSecureTextField *password;
@property(nonatomic, strong) NSButton *remember;
@property(nonatomic, strong) NSTextField *errorLabel;
@property(nonatomic, copy) NSString *savedName;
- (instancetype)initWithNames:(NSArray<NSString *> *)names preferred:(NSString *)preferred editing:(NSString *)editing saveOnly:(BOOL)saveOnly;
- (void)selectSaved:(id)sender;
@end
static void checkLayout(MossNetworkForm *form) {
  assert(NSContainsRect(form.bounds, form.name.frame));
  assert(NSContainsRect(form.bounds, form.password.frame));
  assert(NSContainsRect(form.bounds, form.remember.frame));
  assert(NSContainsRect(form.bounds, form.errorLabel.frame));
  assert(!NSIntersectsRect(form.errorLabel.frame, form.remember.frame));
  assert(!NSIntersectsRect(form.password.frame, form.remember.frame));
  assert(form.name.nextKeyView == form.password);
  assert(form.password.nextKeyView == form.remember);
  assert([form.password isKindOfClass:NSSecureTextField.class]);
}
int main(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    MossNetworkForm *connection = [[MossNetworkForm alloc] initWithNames:@[@"Saved fixture"] preferred:@"Saved fixture" editing:nil saveOnly:NO];
    checkLayout(connection);
    assert(connection.picker.numberOfItems == 2 && connection.picker.indexOfSelectedItem == 1);
    assert([connection.name.stringValue isEqualToString:@"Saved fixture"]);
    assert([connection.savedName isEqualToString:@"Saved fixture"]);
    assert(connection.password.stringValue.length == 0); // No Keychain password is preloaded into UI.
    assert(connection.remember.state == NSControlStateValueOn);
    [connection.picker selectItemAtIndex:0]; [connection selectSaved:nil];
    assert(connection.name.stringValue.length == 0 && !connection.savedName);
    assert(connection.password.stringValue.length == 0);
    MossNetworkForm *add = [[MossNetworkForm alloc] initWithNames:@[] preferred:nil editing:nil saveOnly:YES];
    checkLayout(add); assert(!add.picker && add.name.editable);
    assert(add.remember.state == NSControlStateValueOn);
    MossNetworkForm *edit = [[MossNetworkForm alloc] initWithNames:@[@"Saved fixture"] preferred:@"Saved fixture" editing:@"Saved fixture" saveOnly:YES];
    checkLayout(edit); assert(!edit.name.editable && !edit.savedName);
    assert(edit.password.stringValue.length == 0 && edit.remember.state == NSControlStateValueOn);
    MossNetworkForm *manual = [[MossNetworkForm alloc] initWithNames:@[@"Saved fixture"] preferred:nil editing:@"Saved fixture" saveOnly:YES];
    assert(manual.remember.state == NSControlStateValueOff);
    puts("Network form passed: saved selection, secure empty password field, remember defaults, editor modes, layout bounds and tab order");
  }
}
