#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define UI_IMPLEMENTATION
#include "luigi2.h"

#include "../native_interface.h"

typedef struct ElementWrapper {
	UIElement *element;
	uintptr_t referenceCount;
	intptr_t messageUser;
	intptr_t cp;
} ElementWrapper;

// TODO How should this be done properly?
struct ExecutionContext *contextForCallback;

bool ReturnRectangle(struct ExecutionContext *context, UIRectangle rect) {
	return ScriptReturnStructInl(context, 4, rect.l, false, rect.r, false, rect.t, false, rect.b, false);
}

bool ReturnCString(struct ExecutionContext *context, const char *cString) {
	return ScriptReturnString(context, cString, strlen(cString));
}

void WrapperClose(struct ExecutionContext *context, void *_wrapper) {
	ElementWrapper *wrapper = (ElementWrapper *) _wrapper;

	wrapper->referenceCount--;

	if (wrapper->referenceCount == 0) {
		ScriptHeapRefClose(context, wrapper->messageUser);
		ScriptHeapRefClose(context, wrapper->cp);
		UI_ASSERT(!wrapper->element);
		free(wrapper);
	}
}

bool WrapperOpen(struct ExecutionContext *context, ElementWrapper *wrapper, intptr_t *handle) {
	UI_ASSERT(wrapper->referenceCount);
	wrapper->referenceCount++;
	return ScriptCreateHandle(context, wrapper, WrapperClose, handle);
}

int WrapperMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ElementWrapper *wrapper = (ElementWrapper *) element->cp;

	if (!wrapper) {
		return 0;
	}

	if (wrapper->messageUser) {
		intptr_t elementHandle = -1;
		bool success = WrapperOpen(contextForCallback, wrapper, &elementHandle);
		(void) success;
		// TODO How to handle failure?

		int64_t parameters[4] = {
			(int64_t) elementHandle,
			(int64_t) message,
			(int64_t) di,
			0, // TODO MessageData.
		};

		bool managedParameters[4] = { true, false, false, true };
		// TODO Modify this so that it actually works!
		success = ScriptRunCallback(contextForCallback, wrapper->messageUser, parameters, managedParameters, 4);
		// TODO How to handle failure?

		ScriptHeapRefClose(contextForCallback, elementHandle);
	}

	if (message == UI_MSG_DESTROY) {
		WrapperClose(contextForCallback, wrapper);
		element->cp = NULL;
		wrapper->element = NULL;
	}

	return 0;
}

ElementWrapper *WrapperCreate(UIElement *element) {
	if (element->cp) {
		ElementWrapper *wrapper = (ElementWrapper *) element->cp;
		UI_ASSERT(wrapper->referenceCount);
		wrapper->referenceCount++;
		return wrapper;
	} else {
		ElementWrapper *wrapper = (ElementWrapper *) UI_CALLOC(sizeof(ElementWrapper));
		element->cp = wrapper;
		element->messageUser = WrapperMessage;
		wrapper->element = element;
		wrapper->referenceCount = 2; // One returned here, one held by the element until MSG_DESTROY.
		return wrapper;
	}
}

bool ScriptExtInitialise(struct ExecutionContext *context) {
	UIInitialise();
	return true;
}

bool ScriptExtMessageLoop(struct ExecutionContext *context) {
	contextForCallback = context;
	ScriptReturnInt(context, UIMessageLoop());
	return true;
}

bool ScriptExtWindowCreate(struct ExecutionContext *context) {
	ElementWrapper *owner;
	uint32_t flags;
	char *title = NULL;
	int32_t width, height;

	bool success = ScriptParameterHandle(context, (void **) &owner)
		&& ScriptParameterUint32(context, &flags)
		&& ScriptParameterCString(context, &title)
		&& ScriptParameterInt32(context, &width)
		&& ScriptParameterInt32(context, &height);

	if (success) {
		UIWindow *window = UIWindowCreate(owner ? (UIWindow *) owner->element : NULL, flags, title, width, height);
		success = ScriptReturnHandle(context, WrapperCreate(&window->e), WrapperClose);
	} else {
		success = ScriptReturnHandle(context, NULL, NULL);
	}

	free(title);
	return success;
}

#define SCRIPT_EXT_GET_FIELD(name, type, field, fieldType) \
bool ScriptExt##name(struct ExecutionContext *context) { \
	ElementWrapper *element; \
	return ScriptParameterScan(context, "h", (void **) &element) && ScriptReturn##fieldType(context, ((type *) element->element)->field); \
}
SCRIPT_EXT_GET_FIELD(WindowGetCursorX, UIWindow, cursorX, Int);
SCRIPT_EXT_GET_FIELD(WindowGetCursorY, UIWindow, cursorY, Int);
SCRIPT_EXT_GET_FIELD(WindowGetPressedMouseButton, UIWindow, pressedButton, Int);
SCRIPT_EXT_GET_FIELD(WindowGetScale, UIWindow, scale, Double);
SCRIPT_EXT_GET_FIELD(WindowGetTextboxModifiedFlag, UIWindow, textboxModifiedFlag, Int);
SCRIPT_EXT_GET_FIELD(WindowIsAltHeld, UIWindow, alt, Int);
SCRIPT_EXT_GET_FIELD(WindowIsCtrlHeld, UIWindow, ctrl, Int);
SCRIPT_EXT_GET_FIELD(WindowIsShiftHeld, UIWindow, shift, Int);
SCRIPT_EXT_GET_FIELD(ElementGetFlags, UIElement, flags, Int);
SCRIPT_EXT_GET_FIELD(TextboxGetSelectionFrom, UITextbox, carets[0], Int);
SCRIPT_EXT_GET_FIELD(TextboxGetSelectionTo, UITextbox, carets[1], Int);
SCRIPT_EXT_GET_FIELD(SliderGetPosition, UISlider, position, Double);
SCRIPT_EXT_GET_FIELD(CheckboxGetCheck, UICheckbox, check, Int);

#define SCRIPT_EXT_LABELLED_ELEMENT_CREATE(name) \
bool ScriptExt##name##Create(struct ExecutionContext *context) { \
	ElementWrapper *parent; \
	uint32_t flags; \
	const char *string; \
	size_t stringBytes; \
	bool success = ScriptParameterHandle(context, (void **) &parent) \
		&& ScriptParameterUint32(context, &flags) \
		&& ScriptParameterString(context, (const void **) &string, &stringBytes); \
	if (success) { \
		UI##name *element = UI##name##Create(parent->element, flags, string, stringBytes); \
		return ScriptReturnHandle(context, WrapperCreate(&element->e), WrapperClose); \
	} else { \
		return ScriptReturnHandle(context, NULL, NULL); \
	} \
}
SCRIPT_EXT_LABELLED_ELEMENT_CREATE(Button);
SCRIPT_EXT_LABELLED_ELEMENT_CREATE(Checkbox);
SCRIPT_EXT_LABELLED_ELEMENT_CREATE(Label);

#define SCRIPT_EXT_CSTRING_ELEMENT_CREATE(name) \
bool ScriptExt##name##Create(struct ExecutionContext *context) { \
	ElementWrapper *parent; \
	uint32_t flags; \
	char *tabs; \
	bool success = ScriptParameterHandle(context, (void **) &parent) \
		&& ScriptParameterUint32(context, &flags) \
		&& ScriptParameterCString(context, &tabs); \
	if (success) { \
		UI##name *element = UI##name##Create(parent->element, flags, tabs); \
		success = ScriptReturnHandle(context, WrapperCreate(&element->e), WrapperClose); \
	} else { \
		success = ScriptReturnHandle(context, NULL, NULL); \
	} \
	free(tabs); \
	return success; \
}
SCRIPT_EXT_CSTRING_ELEMENT_CREATE(TabPane);
SCRIPT_EXT_CSTRING_ELEMENT_CREATE(Table);

#define SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(name) \
bool ScriptExt##name##Create(struct ExecutionContext *context) { \
	ElementWrapper *parent; \
	uint32_t flags; \
	bool success = ScriptParameterHandle(context, (void **) &parent) \
		&& ScriptParameterUint32(context, &flags); \
	if (success) { \
		UI##name *element = UI##name##Create(parent->element, flags); \
		return ScriptReturnHandle(context, WrapperCreate(&element->e), WrapperClose); \
	} else { \
		return ScriptReturnHandle(context, NULL, NULL); \
	} \
}
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Code);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Gauge);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(MDIClient);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Panel);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(ScrollBar);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Slider);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Switcher);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(Textbox);
SCRIPT_EXT_SIMPLE_ELEMENT_CREATE(WrapPanel);

bool ScriptExtSpacerCreate(struct ExecutionContext *context) {
	ElementWrapper *parent;
	uint32_t flags;
	int32_t width, height;
	return ScriptParameterScan(context, "huii", &parent, &flags, &width, &height)
		&& ScriptReturnHandle(context, WrapperCreate(&UISpacerCreate(parent->element, flags, width, height)->e), WrapperClose);
}

bool ScriptExtSplitPaneCreate(struct ExecutionContext *context) {
	ElementWrapper *parent;
	uint32_t flags;
	double weight;
	return ScriptParameterScan(context, "huF", &parent, &flags, &weight)
		&& ScriptReturnHandle(context, WrapperCreate(&UISplitPaneCreate(parent->element, flags, weight)->e), WrapperClose);
}

bool ScriptExtExpandPaneCreate(struct ExecutionContext *context) {
	ElementWrapper *parent;
	uint32_t flags, panelFlags;
	const char *label;
	size_t labelBytes;
	return ScriptParameterScan(context, "huSu", &parent, &flags, &label, &labelBytes, &panelFlags)
		&& ScriptReturnHandle(context, WrapperCreate(&UIExpandPaneCreate(parent->element, flags, label, labelBytes, panelFlags)->e), WrapperClose);
}

bool ScriptExtElementSetMessageUser(struct ExecutionContext *context) {
	ElementWrapper *element;
	intptr_t messageCallback = 0;

	bool success = ScriptParameterHandle(context, (void **) &element)
		&& ScriptParameterHeapRef(context, &messageCallback);

	if (success) {
		if (element->messageUser) {
			ScriptHeapRefClose(context, element->messageUser);
		}

		element->messageUser = messageCallback;
	} else {
		if (messageCallback) {
			ScriptHeapRefClose(context, messageCallback);
		}
	}

	return success;
}

bool ScriptExtElementGetClassName(struct ExecutionContext *context) {
	ElementWrapper *element;
	return ScriptParameterHandle(context, (void **) &element) && ReturnCString(context, element->element->cClassName);
}

bool ScriptExtElementGetWindow(struct ExecutionContext *context) {
	ElementWrapper *element;
	return ScriptParameterHandle(context, (void **) &element) && ScriptReturnHandle(context, WrapperCreate(&element->element->window->e), WrapperClose);
}

bool ScriptExtElementGetParent(struct ExecutionContext *context) {
	ElementWrapper *element;
	return ScriptParameterHandle(context, (void **) &element) && ScriptReturnHandle(context, WrapperCreate(element->element->parent), WrapperClose);
}

bool ScriptExtElementSetFlags(struct ExecutionContext *context) {
	ElementWrapper *element;
	uint32_t flags;
	if (!ScriptParameterScan(context, "hu", (void **) &element, &flags)) return false;
	element->element->flags = flags;
	return true;
}

bool ScriptExtElementAnimate(struct ExecutionContext *context) {
	ElementWrapper *element;
	bool stop;
	if (!ScriptParameterScan(context, "hb", (void **) &element, &stop)) return false;
	UIElementAnimate(element->element, stop);
	return true;
}

bool ScriptExtElementChangeParent(struct ExecutionContext *context) {
	ElementWrapper *element, *newParent, *insertBefore;
	return ScriptParameterScan(context, "hhh", &element, &newParent, &insertBefore) && ScriptReturnHandle(context, WrapperCreate(
				UIElementChangeParent(element->element, newParent->element, insertBefore ? insertBefore->element : NULL)), WrapperClose);
}

bool ScriptExtElementDestroy(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	UIElementDestroy(element->element);
	return true;
}

bool ScriptExtElementDestroyDescendents(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	UIElementDestroyDescendents(element->element);
	return true;
}

bool ScriptExtElementFocus(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	UIElementFocus(element->element);
	return true;
}

bool ScriptExtElementRefresh(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtElementRelayout(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	UIElementRelayout(element->element);
	return true;
}

bool ScriptExtElementMeasurementsChanged(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t which;
	if (!ScriptParameterScan(context, "hi", &element, &which)) return false;
	UIElementMeasurementsChanged(element->element, which);
	return true;
}

bool ScriptExtElementFindByPoint(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t x, y;
	return ScriptParameterScan(context, "hii", &element, &x, &y)
		&& ScriptReturnHandle(context, WrapperCreate(UIElementFindByPoint(element->element, x, y)), WrapperClose);
}

bool ScriptExtElementSetContext(struct ExecutionContext *context) {
	ElementWrapper *element;
	intptr_t cp;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	if (!ScriptParameterHeapRef(context, &cp)) return false;
	if (element->cp) ScriptHeapRefClose(context, element->cp);
	element->cp = cp;
	return true;
}

bool ScriptExtElementGetContext(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	return ScriptReturnHeapRef(context, element->cp);
}

bool ScriptExtElementScreenBounds(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	return ReturnRectangle(context, UIElementScreenBounds(element->element));
}

bool ScriptExtElementWindowBounds(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	return ReturnRectangle(context, element->element->bounds);
}

bool ScriptExtElementGetClip(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterHandle(context, (void **) &element)) return false;
	return ReturnRectangle(context, element->element->clip);
}

bool ScriptExtElementMove(struct ExecutionContext *context) {
	ElementWrapper *element;
	UIRectangle rect;
	bool alwaysLayout;
	if (!ScriptParameterScan(context, "h(iiii)b", (void **) &element, &rect.l, &rect.r, &rect.t, &rect.b, &alwaysLayout)) return false;
	UIElementMove(element->element, rect, alwaysLayout);
	return true;
}

bool ScriptExtElementRepaint(struct ExecutionContext *context) {
	ElementWrapper *element;
	UIRectangle rect;
	bool isRectNull;
	if (!ScriptParameterScan(context, "h(niiii)", (void **) &element, &isRectNull, &rect.l, &rect.r, &rect.t, &rect.b)) return false;
	UIElementRepaint(element->element, isRectNull ? NULL : &rect);
	return true;
}

bool ScriptExtButtonSetLabel(struct ExecutionContext *context) {
	ElementWrapper *element;
	const char *label; size_t labelBytes;
	if (!ScriptParameterScan(context, "hS", (void **) &element, &label, &labelBytes)) return false;
	UIButtonSetLabel((UIButton *) element->element, label, labelBytes);
	return true;
}

bool ScriptExtLabelSetContent(struct ExecutionContext *context) {
	ElementWrapper *element;
	const char *label; size_t labelBytes;
	if (!ScriptParameterScan(context, "hS", (void **) &element, &label, &labelBytes)) return false;
	UILabelSetContent((UILabel *) element->element, label, labelBytes);
	return true;
}

bool ScriptExtPanelSetSpacing(struct ExecutionContext *context) {
	ElementWrapper *element;
	UIRectangle border; int gap;
	if (!ScriptParameterScan(context, "h(iiii)i", (void **) &element, &border.l, &border.r, &border.t, &border.b, &gap)) return false;
	UIPanel *panel = (UIPanel *) element->element;
	panel->border = border;
	panel->gap = gap;
	UIElementMeasurementsChanged(element->element, 3);
	return true;
}

bool ScriptExtGaugeSetPosition(struct ExecutionContext *context) {
	ElementWrapper *element;
	double value;
	if (!ScriptParameterScan(context, "hF", (void **) &element, &value)) return false;
	UIGaugeSetPosition((UIGauge *) element->element, value);
	return true;
}

bool ScriptExtTextboxReplace(struct ExecutionContext *context) {
	ElementWrapper *element;
	const char *text; size_t textBytes; bool sendChangedMessage;
	if (!ScriptParameterScan(context, "hSb", (void **) &element, &text, &textBytes, &sendChangedMessage)) return false;
	UITextboxReplace((UITextbox *) element->element, text, textBytes, sendChangedMessage);
	return true;
}

bool ScriptExtTextboxClear(struct ExecutionContext *context) {
	ElementWrapper *element;
	bool sendChangedMessage;
	if (!ScriptParameterScan(context, "hb", (void **) &element, &sendChangedMessage)) return false;
	UITextboxClear((UITextbox *) element->element, sendChangedMessage);
	return true;
}

bool ScriptExtTextboxMoveCaret(struct ExecutionContext *context) {
	ElementWrapper *element;
	bool backward, word;
	if (!ScriptParameterScan(context, "hbb", (void **) &element, &backward, &word)) return false;
	UITextboxMoveCaret((UITextbox *) element->element, backward, word);
	return true;
}

bool ScriptExtTextboxGetContents(struct ExecutionContext *context) {
	ElementWrapper *element;
	return ScriptParameterScan(context, "h", (void **) &element)
		&& ScriptReturnString(context, ((UITextbox *) element->element)->string, ((UITextbox *) element->element)->bytes);
}

bool ScriptExtTextboxRejectNextKey(struct ExecutionContext *context) {
	ElementWrapper *element;
	if (!ScriptParameterScan(context, "h", (void **) &element)) return false;
	((UITextbox *) element->element)->rejectNextKey = true;
	return true;
}

bool ScriptExtTextboxSetSelectionFrom(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t index;
	if (!ScriptParameterScan(context, "hi", (void **) &element, &index)) return false;
	((UITextbox *) element->element)->carets[0] = index;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtTextboxSetSelectionTo(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t index;
	if (!ScriptParameterScan(context, "hi", (void **) &element, &index)) return false;
	((UITextbox *) element->element)->carets[1] = index;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtTextboxSetCaretPosition(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t index;
	if (!ScriptParameterScan(context, "hi", (void **) &element, &index)) return false;
	((UITextbox *) element->element)->carets[0] = index;
	((UITextbox *) element->element)->carets[1] = index;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtSliderSetSteps(struct ExecutionContext *context) {
	ElementWrapper *element;
	int32_t steps;
	if (!ScriptParameterScan(context, "hi", (void **) &element, &steps)) return false;
	((UISlider *) element->element)->steps = steps;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtSliderSetPosition(struct ExecutionContext *context) {
	ElementWrapper *element;
	double position; bool sendChangedMessage;
	if (!ScriptParameterScan(context, "hFb", (void **) &element, &position, &sendChangedMessage)) return false;
	UISliderSetPosition((UISlider *) element->element, position, sendChangedMessage);
	return true;
}

bool ScriptExtCheckboxSetCheck(struct ExecutionContext *context) {
	ElementWrapper *element;
	int check;
	if (!ScriptParameterScan(context, "hi", (void **) &element, &check)) return false;
	((UICheckbox *) element->element)->check = check;
	UIElementRefresh(element->element);
	return true;
}

bool ScriptExtRectangleContains(struct ExecutionContext *context) {
	UIRectangle rect;
	int32_t x, y;
	return ScriptParameterScan(context, "(iiii)ii", &rect.l, &rect.r, &rect.t, &rect.b, &x, &y)
		&& ScriptReturnInt(context, UIRectangleContains(rect, x, y));
}

bool ScriptExtRectangleEquals(struct ExecutionContext *context) {
	UIRectangle rect1, rect2;
	return ScriptParameterScan(context, "(iiii)(iiii)", &rect1.l, &rect1.r, &rect1.t, &rect1.b, &rect2.l, &rect2.r, &rect2.t, &rect2.b)
		&& ScriptReturnInt(context, UIRectangleEquals(rect1, rect2));
}

#define SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(name) \
bool ScriptExtRectangle##name(struct ExecutionContext *context) { \
	UIRectangle rect1, rect2; \
	return ScriptParameterScan(context, "(iiii)(iiii)", &rect1.l, &rect1.r, &rect1.t, &rect1.b, &rect2.l, &rect2.r, &rect2.t, &rect2.b) \
		&& ReturnRectangle(context, UIRectangle##name(rect1, rect2)); \
}
SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(Add);
SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(Intersection);
SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(Bounding);
SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(Translate);
SCRIPT_EXT_RECTANGLE_BINARY_OPERATOR(Center);

bool ScriptExtRectangleFit(struct ExecutionContext *context) {
	UIRectangle rect1, rect2;
	bool allowScalingUp;
	return ScriptParameterScan(context, "(iiii)(iiii)b", &rect1.l, &rect1.r, &rect1.t, &rect1.b, &rect2.l, &rect2.r, &rect2.t, &rect2.b, &allowScalingUp)
		&& ReturnRectangle(context, UIRectangleFit(rect1, rect2, allowScalingUp));
}

bool ScriptExtKeycodeBackspace(struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_BACKSPACE); }
bool ScriptExtKeycodeDelete   (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_DELETE   ); }
bool ScriptExtKeycodeDown     (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_DOWN     ); }
bool ScriptExtKeycodeEnd      (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_END      ); }
bool ScriptExtKeycodeEnter    (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_ENTER    ); }
bool ScriptExtKeycodeEscape   (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_ESCAPE   ); }
bool ScriptExtKeycodeHome     (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_HOME     ); }
bool ScriptExtKeycodeInsert   (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_INSERT   ); }
bool ScriptExtKeycodeLeft     (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_LEFT     ); }
bool ScriptExtKeycodeRight    (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_RIGHT    ); }
bool ScriptExtKeycodeSpace    (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_SPACE    ); }
bool ScriptExtKeycodeTab      (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_TAB      ); }
bool ScriptExtKeycodeUp       (struct ExecutionContext *context) { return ScriptReturnInt(context, UI_KEYCODE_UP       ); }

bool ScriptExtKeycodeFKey(struct ExecutionContext *context) {
	int32_t n;
	return ScriptParameterInt32(context, &n) && ScriptReturnInt(context, UI_KEYCODE_FKEY(n)); 
}

bool ScriptExtKeycodeDigit(struct ExecutionContext *context) {
	const char *s; size_t b;
	return ScriptParameterString(context, (const void **) &s, &b) && ScriptReturnInt(context, b ? UI_KEYCODE_DIGIT(s[0]) : 0); 
}

bool ScriptExtKeycodeLetter(struct ExecutionContext *context) {
	const char *s; size_t b;
	return ScriptParameterString(context, (const void **) &s, &b) && ScriptReturnInt(context, b ? UI_KEYCODE_LETTER(s[0]) : 0); 
}
