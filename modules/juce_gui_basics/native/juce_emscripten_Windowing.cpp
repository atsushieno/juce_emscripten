/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

extern juce::JUCEApplicationBase* juce_CreateApplication(); // (from START_JUCE_APPLICATION)

//==============================================================================
void launchApp()
{
    using namespace juce;

    DBG (SystemStats::getJUCEVersion());

    JUCEApplicationBase::createInstance = &juce_CreateApplication;

    initialiseJuce_GUI();

    JUCEApplicationBase* app = JUCEApplicationBase::createInstance();
    if (! app->initialiseApp())
        exit (app->getApplicationReturnValue());

    jassert (MessageManager::getInstance()->isThisTheMessageThread());

    MessageManager::getInstance()->runDispatchLoop();
}

#include <emscripten.h>

namespace juce
{

class EmscriptenComponentPeer;

Point<int> recentMousePosition;
Array<EmscriptenComponentPeer*> emComponentPeerList;

EM_JS(void, attachEventCallbackToWindow, (),
{
    if (window.juce_mouseCallback) return;

    // event name, x, y, which button, shift, ctrl, alt, wheel delta
    window.juce_mouseCallback = Module.cwrap(
        'juce_mouseCallback', 'void', ['string', 'number', 'number', 'number',
        'number', 'number', 'number', 'number']);
    
    window.onmousedown  = function(e) {
        window.juce_mouseCallback('down' ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmouseup    = function(e) { 
        window.juce_mouseCallback('up'   ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmousewheel = function(e) { 
        window.juce_mouseCallback('wheel',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, e.wheelDelta);
    };
    window.onmouseenter = function(e) { 
        window.juce_mouseCallback('enter',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmouseleave = function(e) { 
        window.juce_mouseCallback('leave',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmousemove  = function(e) { 
        window.juce_mouseCallback('move' ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };

    // window.onmouseout   = function(e) { 
    //     window.juce_mouseCallback('out'  , e.pageX, e.pageY, e.which); };
    // window.onmouseover  = function(e) { 
    //     window.juce_mouseCallback('over' , e.pageX, e.pageY, e.which); };

    // event name, key code, key
    window.juce_keyboardCallback = Module.cwrap(
        'juce_keyboardCallback', 'void', ['string', 'number', 'string']);
    
    window.addEventListener('keydown', function(e) {
        window.juce_keyboardCallback('down', e.which || e.keyCode, e.key);
    });
    window.addEventListener('keyup', function(e) {
        window.juce_keyboardCallback('up', e.which || e.keyCode, e.key);
    });

    window.juce_clipboard = "";

    window.addEventListener('copy', function(e) {
        navigator.clipboard.readText().then(function(text) {
            window.juce_clipboard = text;
        });
    });
});

class EmscriptenComponentPeer : public ComponentPeer,
                                public MessageListener
{
    Rectangle<int> bounds;
    String id;
    static int highestZIndex;
    int zIndex{0};
    bool focused{false};

    RectangleList<int> pendingRepaintAreas;

    struct RepaintMessage : public Message {};

    public:
        EmscriptenComponentPeer(Component &component, int styleFlags)
        :ComponentPeer(component, styleFlags)
        {
            emComponentPeerList.add(this);
            std::cout << "EmscriptenComponentPeer" << std::endl;

            id = Uuid().toDashedString();

            std::cout << "id is " << id << std::endl;

            attachEventCallbackToWindow();

            EM_ASM_INT({
                var canvas = document.createElement('canvas');
                canvas.id  = UTF8ToString($0);
                canvas.style.zIndex   = $6;
                console.log($6, canvas.style.zIndex);
                canvas.style.position = "absolute";
                canvas.style.border   = "1px solid";
                canvas.style.left = $1;
                canvas.style.top  = $2;
                canvas.width  = $3;
                canvas.height = $4;
                canvas.oncontextmenu = function(e) { e.preventDefault(); };
                canvas.setAttribute('data-peer', $5);
                document.body.appendChild(canvas);
            }, id.toRawUTF8(), bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), this, ++highestZIndex);
            
            zIndex = highestZIndex;

            grabFocus();

            //if (isFocused())
            //    handleFocusGain();
        }

        ~EmscriptenComponentPeer()
        {
            emComponentPeerList.removeAllInstancesOf(this);
            EM_ASM_ARGS({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.parentElement.removeChild(canvas);
            }, id.toRawUTF8());
        }

        int getZIndex () const { return zIndex; }

        struct ZIndexComparator
        {
            int compareElements (const EmscriptenComponentPeer* first,
                                 const EmscriptenComponentPeer* second) {
                if(first -> getZIndex() < second -> getZIndex()) return -1;
                if(first -> getZIndex() > second -> getZIndex()) return 1;
                return 0;
            }
        };

        virtual void* getNativeHandle () const override
        {
            return (void*)this;
        }

        virtual void setVisible (bool shouldBeVisible) override
        {

        }

        virtual void setTitle (const String &title) override
        {
            std::cout << "setTitle: " << title << std::endl;
        }

        virtual void setBounds (const Rectangle< int > &newBounds, bool isNowFullScreen) override
        {
            std::cout << "setBounds " << newBounds.toString().toStdString() << std::endl;

            auto oldBounds = bounds;
            
            EM_ASM_ARGS({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.left = $1 + 'px';
                canvas.style.top  = $2 + 'px';
                if(canvas.width  != $3) canvas.width  = $3;
                if(canvas.height != $4) canvas.height = $4;
            }, id.toRawUTF8(), newBounds.getX(), newBounds.getY(), newBounds.getWidth(), newBounds.getHeight());
            
            bounds = newBounds;
            fullscreen = isNowFullScreen;

            handleMovedOrResized();

            if (! newBounds.isEmpty() &&
                newBounds.withZeroOrigin() != oldBounds.withZeroOrigin())
            {
                repaint(newBounds.withZeroOrigin());
            }
        }

        virtual Rectangle<int> getBounds () const override
        {
            return bounds;
        }

        virtual Point<float>  localToGlobal (Point< float > relativePosition) override
        {
            return relativePosition + bounds.getPosition().toFloat();
        }

        virtual Point<float>  globalToLocal (Point< float > screenPosition) override
        {
            return screenPosition - bounds.getPosition().toFloat();
        }

        virtual void setMinimised (bool shouldBeMinimised) override
        {

        }

        virtual bool isMinimised () const override
        {
            return false;
        }

        bool fullscreen=false;
        virtual void setFullScreen (bool shouldBeFullScreen) override
        {
            EM_ASM_ARGS({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.left='0px';
                canvas.style.top ='0px';

                canvas.width=window.innerWidth;
                canvas.height=window.innerHeight;
            }, id.toRawUTF8());

            bounds = bounds.withZeroOrigin();

            bounds.setWidth ( EM_ASM_ARGS({ return window.innerWidth; }, 0) );
            bounds.setHeight( EM_ASM_ARGS({ return window.innerHeight; }, 0) );

            this->setBounds(bounds, true);
        }

        virtual bool isFullScreen () const override
        {
            return fullscreen;
        }

        virtual void setIcon (const Image &newIcon) override
        {

        }

        virtual bool contains (Point< int > localPos, bool trueIfInAChildWindow) const override
        {
            Point<int> globalPos = localPos+bounds.getPosition();
            return bounds.contains(globalPos);
        }

        virtual BorderSize< int >   getFrameSize () const override
        {
            return BorderSize<int>();
        }

        virtual bool setAlwaysOnTop (bool alwaysOnTop) override
        {
            return false;
        }

        virtual void toFront (bool makeActive) override
        {
            std::clog << "toFront " << id << " " << makeActive <<  std::endl;

            highestZIndex = EM_ASM_INT({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.zIndex = parseInt($1)+1;
                console.log(canvas.style.zIndex, $1);
                return parseInt(canvas.style.zIndex);
            }, id.toRawUTF8(), highestZIndex);

            zIndex = highestZIndex;

            handleBroughtToFront();

            if (makeActive)
            {
                grabFocus();
            }
        }

        void updateZIndex()
        {
            zIndex = EM_ASM_INT({
                var canvas = document.getElementById(UTF8ToString($0));
                return canvas.zIndex;
            }, id.toRawUTF8());
        }

        virtual void toBehind (ComponentPeer *other) override
        {
            std::clog << "toBehind" << std::endl;

            if(EmscriptenComponentPeer* otherPeer = dynamic_cast<EmscriptenComponentPeer*>(other))
            {
                int newZIndex = EM_ASM_INT({
                    var canvas = document.getElementById(UTF8ToString($0));
                    var other  = document.getElementById(UTF8ToString($1));
                    canvas.zIndex = parseInt(other.zIndex)-1;
                    return parseInt(other.zIndex);
                }, id.toRawUTF8(), otherPeer->id.toRawUTF8());

                highestZIndex = std::max(highestZIndex, newZIndex);

                updateZIndex();
                otherPeer->updateZIndex();
                if (! otherPeer->isFocused())
                {
                    otherPeer->focused = true;
                    otherPeer->handleFocusGain();
                }
            }

            if (focused)
            {
                focused = false;
                handleFocusLoss();
            }
        }

        virtual bool isFocused() const override
        {
            return focused;
        }

        virtual void grabFocus() override
        {
            std::clog << "grabFocus " << id << std::endl;
            if (! focused)
            {
                for (auto* other : emComponentPeerList)
                {
                    if (other != this && other -> isFocused())
                    {
                        other -> focused = false;
                        other -> handleFocusLoss();
                    }
                }
                focused = true;
                handleFocusGain();
            }
        }

        virtual void textInputRequired(Point< int > position, TextInputTarget &) override
        {
            std::cout << "textInputRequired" << std::endl;
        }

        virtual void repaint (const Rectangle<int>& area) override
        {
            pendingRepaintAreas.add(area);
            postMessage(new RepaintMessage());
        }

        virtual void performAnyPendingRepaintsNow() override
        {
            std::cout << "performAnyPendingRepaintsNow" << std::endl;
        }

        virtual void setAlpha (float newAlpha) override
        {
            std::cout << "setAlpha" << std::endl;
        }

        virtual StringArray getAvailableRenderingEngines() override
        {
            return StringArray();
        }

    private:

        void handleMessage (const Message& msg) override
        {
            if (dynamic_cast<const RepaintMessage*>(& msg))
            {
                for (int i = 0; i < pendingRepaintAreas.getNumRectangles(); i ++)
                {
                    Rectangle<int> area = pendingRepaintAreas.getRectangle(i);
                    internalRepaint(area);
                }
                pendingRepaintAreas.clear();
            }
        }

        void internalRepaint (const Rectangle<int> &area)
        {
            std::cout << "repaint: " << area.toString().toStdString() << std::endl;

            Image temp(Image::ARGB, area.getWidth(), area.getHeight(), true);
            LowLevelGraphicsSoftwareRenderer g(temp);
            g.setOrigin (-area.getPosition());
            handlePaint (g);

            /*{
                Graphics g_(temp);
                g_.setColour(Colours::red);
                g_.fillEllipse(0,0,100,100);
                g_.setColour(Colours::green);
                g_.fillEllipse(100,0,100,100);
                g_.setColour(Colours::blue);
                g_.fillEllipse(0,100,100,100);
            }*/

            Image::BitmapData bitmapData(temp, Image::BitmapData::readOnly);

            EM_ASM_ARGS({
                var id = UTF8ToString($0);
                var pointer = $1;
                var width   = $2;
                var height  = $3;
                var dest_x  = $4;
                var dest_y  = $5;

                var canvas = document.getElementById(id);
                var ctx = canvas.getContext("2d");

                var buffer = new Uint8ClampedArray(Module.HEAPU8.buffer, pointer, width*height*4);

                for (var idx = 0; idx < buffer.length; idx += 4)
                {
                    var r = buffer[idx+0];
                    var b = buffer[idx+2];

                    buffer[idx+0] = b;
                    buffer[idx+2] = r;
                }

                var imageData = ctx.createImageData(width, height);
                imageData.data.set(buffer);

                var tmp = document.createElement('canvas');
                tmp.width = width;
                tmp.height = height;
                var tmp_ctx = tmp.getContext("2d");
                tmp_ctx.putImageData(imageData, 0,0);

                ctx.drawImage(tmp, dest_x, dest_y);

                delete tmp;
                delete buffer;
            }, id.toRawUTF8(), bitmapData.getPixelPointer(0,0), temp.getWidth(), temp.getHeight(), area.getX(), area.getY());
        }

};

int EmscriptenComponentPeer::highestZIndex = 10;
int64 fakeMouseEventTime = 0;

extern "C" void juce_mouseCallback(const char* type, int x, int y, int which,
    int isShiftDown, int isCtrlDown, int isAltDown, int wheelDelta)
{
    // std::clog << type << " " << x << " " << y << " " << which
    //           << " " << isShiftDown << " " << wheelDelta << std::endl;
    recentMousePosition = {x, y};

    ModifierKeys& mods = ModifierKeys::currentModifiers;

    if (type == std::string("down"))
    {
        mods = mods.withoutMouseButtons();
        if (which == 0 || which > 2)
            mods = mods.withFlags(ModifierKeys::leftButtonModifier);
        else if (which == 1)
            mods = mods.withFlags(ModifierKeys::middleButtonModifier);
        else if (which == 2)
            mods = mods.withFlags(ModifierKeys::rightButtonModifier);
    }
    else if (type == std::string("up"))
    {
        mods = mods.withoutMouseButtons();
    }
    
    mods = isShiftDown ? mods.withFlags(ModifierKeys::shiftModifier)
                       : mods.withoutFlags(ModifierKeys::shiftModifier);
    mods = isCtrlDown  ? mods.withFlags(ModifierKeys::ctrlModifier)
                       : mods.withoutFlags(ModifierKeys::ctrlModifier);
    mods = isAltDown   ? mods.withFlags(ModifierKeys::altModifier)
                       : mods.withoutFlags(ModifierKeys::altModifier);
    
    EmscriptenComponentPeer::ZIndexComparator comparator;
    emComponentPeerList.sort(comparator);

    for (int i = emComponentPeerList.size() - 1; i >= 0; i --)
    {
        EmscriptenComponentPeer* peer = emComponentPeerList[i];
        Point<float> pos = peer->globalToLocal(Point<float>(x, y));

        if (wheelDelta == 0)
        {
            peer->handleMouseEvent(MouseInputSource::InputSourceType::mouse,
                pos, mods, MouseInputSource::invalidPressure, 0.0f, fakeMouseEventTime);
        } else
        {
            MouseWheelDetails wheelInfo;
            wheelInfo.deltaX = 0.0f;
            wheelInfo.deltaY = wheelDelta / 480.0f;
            wheelInfo.isReversed = false;
            wheelInfo.isSmooth = false;
            wheelInfo.isInertial = false;
            peer->handleMouseWheel(MouseInputSource::InputSourceType::mouse,
                pos, fakeMouseEventTime, wheelInfo);
        }
        fakeMouseEventTime ++;
    }
}

extern "C" void juce_keyboardCallback(const char* type, int keyCode, const char * key)
{
    // std::cout << "key " << type << " " << keyCode << " " << key << std::endl;
    bool isChar = strlen(key) == 1;
    bool isDown = type == std::string("down");
    juce_wchar keyChar = isChar ? (juce_wchar)key[0] : 0;

    ModifierKeys& mods = ModifierKeys::currentModifiers;
    auto changedModifier = ModifierKeys::noModifiers;
    if (keyCode == 16)
        changedModifier = ModifierKeys::shiftModifier;
    else if (keyCode == 17)
        changedModifier = ModifierKeys::ctrlModifier;
    else if (keyCode == 18)
        changedModifier = ModifierKeys::altModifier;
    else if (keyCode == 91)
        changedModifier = ModifierKeys::commandModifier;
    
    if (changedModifier != ModifierKeys::noModifiers)
        mods = isDown ? mods.withFlags(changedModifier)
                      : mods.withoutFlags(changedModifier);
    
    if ((keyChar >= 'a' && keyChar <= 'z') ||
        (keyChar >= 'A' && keyChar <= 'Z'))
        keyCode = keyChar;
    
    for (int i = emComponentPeerList.size() - 1; i >= 0; i --)
    {
        EmscriptenComponentPeer* peer = emComponentPeerList[i];
        if (changedModifier != ModifierKeys::noModifiers)
            peer->handleModifierKeysChange();
        peer->handleKeyUpOrDown(isDown);
        if (isDown)
            peer->handleKeyPress(KeyPress(keyCode, mods, keyChar));
    }
}

//==============================================================================
ComponentPeer* Component::createNewPeer (int styleFlags, void*)
{
    return new EmscriptenComponentPeer(*this, styleFlags);
}

//==============================================================================
bool Desktop::canUseSemiTransparentWindows() noexcept
{
    return true;
}

double Desktop::getDefaultMasterScale()
{
    return 1.0;
}

Desktop::DisplayOrientation Desktop::getCurrentOrientation() const
{
    // TODO
    return upright;
}

bool MouseInputSource::SourceList::addSource()
{
    addSource(sources.size(), MouseInputSource::InputSourceType::mouse);
    return true;
}

bool MouseInputSource::SourceList::canUseTouch()
{
    return false;
}

Point<float> MouseInputSource::getCurrentRawMousePosition()
{
    return recentMousePosition.toFloat();
}

void MouseInputSource::setRawMousePosition (Point<float>)
{
    // not needed
}

//==============================================================================
bool KeyPress::isKeyCurrentlyDown (const int keyCode)
{
    // TODO
    return false;
}

//==============================================================================
// TODO
JUCE_API bool JUCE_CALLTYPE Process::isForegroundProcess() { return true; }
JUCE_API void JUCE_CALLTYPE Process::makeForegroundProcess() {}
JUCE_API void JUCE_CALLTYPE Process::hide() {}

//==============================================================================
void Desktop::setScreenSaverEnabled (const bool isEnabled)
{
    // TODO
}

bool Desktop::isScreenSaverEnabled()
{
    return true;
}

//==============================================================================
void Desktop::setKioskComponent (Component* kioskModeComponent, bool enableOrDisable, bool allowMenusAndBars)
{
    // TODO
}

//==============================================================================
bool juce_areThereAnyAlwaysOnTopWindows()
{
    return false;
}

//==============================================================================
void Displays::findDisplays (float masterScale)
{
    Display d;
    int width = EM_ASM_INT({
        return document.documentElement.clientWidth;
    });
    int height = EM_ASM_INT({
        return document.documentElement.scrollHeight;
    });
    
    d.totalArea = (Rectangle<float>(width, height) / masterScale).toNearestIntEdges();
    d.userArea = d.totalArea;
    d.isMain = true;
    d.scale = masterScale;
    d.dpi = 70;

    displays.add(d);
}

//==============================================================================
Image juce_createIconForFile (const File& file)
{
    return Image();
}

//==============================================================================
void* CustomMouseCursorInfo::create() const                                                     { return nullptr; }
void* MouseCursor::createStandardMouseCursor (const MouseCursor::StandardCursorType)            { return nullptr; }
void MouseCursor::deleteMouseCursor (void* const /*cursorHandle*/, const bool /*isStandard*/)   {}

//==============================================================================
void MouseCursor::showInWindow (ComponentPeer*) const   {}

//==============================================================================
void LookAndFeel::playAlertSound()
{
}

//==============================================================================
void SystemClipboard::copyTextToClipboard (const String& text)
{
    EM_ASM({
        if (navigator.clipboard)
        {   // async copy
            navigator.clipboard.writeText(UTF8ToString($0));
        } else
        {   // fallback
            var textArea = document.createElement("textarea");
            textArea.value = UTF8ToString($0);
            textArea.style.position = "fixed";
            document.body.appendChild(textArea);
            textArea.focus();
            textArea.select();
            document.execCommand('copy');
            document.body.removeChild(textArea);
        }
    }, text.toRawUTF8());
}

EM_JS(const char*, emscriptenGetClipboard, (), {
    if (window.clipboardUpdater == undefined)
    {
        clipboardUpdater = function(e) {
            navigator.clipboard.readText().then(function(text) {
                window.juce_clipboard = text;
            });
        };
        window.setInterval(clipboardUpdater, 200);
    }
    var data = window.juce_clipboard;
    var dataLen = lengthBytesUTF8(data) + 1;
    var dataOnWASMHeap = _malloc(dataLen);
    stringToUTF8(data, dataOnWASMHeap, dataLen);
    return dataOnWASMHeap;
});

String SystemClipboard::getTextFromClipboard()
{
    const char* data = emscriptenGetClipboard();
    String ret(data);
    free((void*)data);
    return ret;
}

// TODO: make async
void JUCE_CALLTYPE NativeMessageBox::showMessageBoxAsync (
    AlertWindow::AlertIconType iconType,
    const String &title,
    const String &message,
    Component *associatedComponent,
    ModalComponentManager::Callback *callback)
{
    EM_ASM_ARGS({
        alert( UTF8ToString($0) );
    }, message.toRawUTF8());

    if(callback != nullptr) callback->modalStateFinished(1);
}

bool JUCE_CALLTYPE NativeMessageBox::showOkCancelBox(
    AlertWindow::AlertIconType iconType,
    const String &title,
    const String &message,
    Component *associatedComponent,
    ModalComponentManager::Callback *callback)
{
    int result = EM_ASM_ARGS({
        return window.confirm( UTF8ToString($0) );
    }, message.toRawUTF8());
    if(callback != nullptr) callback->modalStateFinished(result?1:0);

    return result?1:0;
}


bool DragAndDropContainer::performExternalDragDropOfFiles (
    const StringArray& files, bool canMoveFiles, Component* sourceComp,
    std::function<void()> callback)
{
    return false;
}

bool DragAndDropContainer::performExternalDragDropOfText (
    const String& text, Component* sourceComp, std::function<void()> callback)
{
    return false;
}

//==============================================================================
const int extendedKeyModifier       = 0x10000;

const int KeyPress::spaceKey        = 32;
const int KeyPress::returnKey       = 13;
const int KeyPress::escapeKey       = 27;
const int KeyPress::backspaceKey    = 8;
const int KeyPress::leftKey         = 37;
const int KeyPress::rightKey        = 39;
const int KeyPress::upKey           = 38;
const int KeyPress::downKey         = 40;
const int KeyPress::pageUpKey       = 33;
const int KeyPress::pageDownKey     = 34;
const int KeyPress::endKey          = 35;
const int KeyPress::homeKey         = 36;
const int KeyPress::deleteKey       = 46;
const int KeyPress::insertKey       = 45;
const int KeyPress::tabKey          = 9;
const int KeyPress::F1Key           = 112;
const int KeyPress::F2Key           = 113;
const int KeyPress::F3Key           = 114;
const int KeyPress::F4Key           = 115;
const int KeyPress::F5Key           = 116;
const int KeyPress::F6Key           = 117;
const int KeyPress::F7Key           = 118;
const int KeyPress::F8Key           = 119;
const int KeyPress::F9Key           = 120;
const int KeyPress::F10Key          = 121;
const int KeyPress::F11Key          = 122;
const int KeyPress::F12Key          = 123;
const int KeyPress::F13Key          = extendedKeyModifier + 24;
const int KeyPress::F14Key          = extendedKeyModifier + 25;
const int KeyPress::F15Key          = extendedKeyModifier + 26;
const int KeyPress::F16Key          = extendedKeyModifier + 27;
const int KeyPress::F17Key          = extendedKeyModifier + 28;
const int KeyPress::F18Key          = extendedKeyModifier + 29;
const int KeyPress::F19Key          = extendedKeyModifier + 30;
const int KeyPress::F20Key          = extendedKeyModifier + 31;
const int KeyPress::F21Key          = extendedKeyModifier + 32;
const int KeyPress::F22Key          = extendedKeyModifier + 33;
const int KeyPress::F23Key          = extendedKeyModifier + 34;
const int KeyPress::F24Key          = extendedKeyModifier + 35;
const int KeyPress::F25Key          = extendedKeyModifier + 36;
const int KeyPress::F26Key          = extendedKeyModifier + 37;
const int KeyPress::F27Key          = extendedKeyModifier + 38;
const int KeyPress::F28Key          = extendedKeyModifier + 39;
const int KeyPress::F29Key          = extendedKeyModifier + 40;
const int KeyPress::F30Key          = extendedKeyModifier + 41;
const int KeyPress::F31Key          = extendedKeyModifier + 42;
const int KeyPress::F32Key          = extendedKeyModifier + 43;
const int KeyPress::F33Key          = extendedKeyModifier + 44;
const int KeyPress::F34Key          = extendedKeyModifier + 45;
const int KeyPress::F35Key          = extendedKeyModifier + 46;
const int KeyPress::numberPad0      = extendedKeyModifier + 27;
const int KeyPress::numberPad1      = extendedKeyModifier + 28;
const int KeyPress::numberPad2      = extendedKeyModifier + 29;
const int KeyPress::numberPad3      = extendedKeyModifier + 30;
const int KeyPress::numberPad4      = extendedKeyModifier + 31;
const int KeyPress::numberPad5      = extendedKeyModifier + 32;
const int KeyPress::numberPad6      = extendedKeyModifier + 33;
const int KeyPress::numberPad7      = extendedKeyModifier + 34;
const int KeyPress::numberPad8      = extendedKeyModifier + 35;
const int KeyPress::numberPad9      = extendedKeyModifier + 36;
const int KeyPress::numberPadAdd            = extendedKeyModifier + 37;
const int KeyPress::numberPadSubtract       = extendedKeyModifier + 38;
const int KeyPress::numberPadMultiply       = extendedKeyModifier + 39;
const int KeyPress::numberPadDivide         = extendedKeyModifier + 40;
const int KeyPress::numberPadSeparator      = extendedKeyModifier + 41;
const int KeyPress::numberPadDecimalPoint   = extendedKeyModifier + 42;
const int KeyPress::numberPadEquals         = extendedKeyModifier + 43;
const int KeyPress::numberPadDelete         = extendedKeyModifier + 44;
const int KeyPress::playKey         = extendedKeyModifier + 45;
const int KeyPress::stopKey         = extendedKeyModifier + 46;
const int KeyPress::fastForwardKey  = extendedKeyModifier + 47;
const int KeyPress::rewindKey       = extendedKeyModifier + 48;

}