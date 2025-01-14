#include "addons/dualdirectional.h"
#include "storagemanager.h"

bool DualDirectionalInput::available() {
    AddonOptions options = Storage::getInstance().getAddonOptions();
    pinDualDirDown = options.pinDualDirDown;
    pinDualDirUp = options.pinDualDirUp;
    pinDualDirLeft = options.pinDualDirLeft;
    pinDualDirRight = options.pinDualDirRight;

    return options.DualDirectionalInputEnabled;
}

void DualDirectionalInput::setup() {
    AddonOptions options = Storage::getInstance().getAddonOptions();
    combineMode = options.dualDirCombineMode;

    dpadMode = options.dualDirDpadMode;

    // Setup TURBO Key
    uint8_t pinDualDir[4] = {pinDualDirDown,
                        pinDualDirUp,
                        pinDualDirLeft,
                        pinDualDirRight};

    for (int i = 0; i < 4; i++) {
        if ( pinDualDir[i] != (uint8_t)-1 ) {
            gpio_init(pinDualDir[i]);             // Initialize pin
            gpio_set_dir(pinDualDir[i], GPIO_IN); // Set as INPUT
            gpio_pull_up(pinDualDir[i]);          // Set as PULLUP
        }
    }

    dDebState = 0;
    dualState = 0;

    lastGPUD = DIRECTION_NONE;
	lastGPLR = DIRECTION_NONE;

    lastDualUD = DIRECTION_NONE;
    lastDualLR = DIRECTION_NONE;

    uint32_t now = getMillis();
    for(int i = 0; i < 4; i++) {
        dpadTime[i] = now;
    }
}

void DualDirectionalInput::debounce()
{
	uint32_t now = getMillis();
    Gamepad * gamepad = Storage::getInstance().GetGamepad();

	for (int i = 0; i < 4; i++)
	{
		if ((dDebState & dpadMasks[i]) != (dualState & dpadMasks[i]) && (now - dpadTime[i]) > gamepad->debounceMS)
		{
			dDebState ^= dpadMasks[i];
			dpadTime[i] = now;
		}
	}
    dualState = dDebState;
}

void DualDirectionalInput::preprocess()
{
    Gamepad * gamepad = Storage::getInstance().GetGamepad();

 	// Need to invert since we're using pullups
    dualState = 0;
    if ( pinDualDirUp != (uint8_t)-1 ) {
        dualState |= (!gpio_get(pinDualDirUp) ? gamepad->mapDpadUp->buttonMask : 0);
    }
    if ( pinDualDirDown != (uint8_t)-1 ) {
        dualState |= (!gpio_get(pinDualDirDown) ? gamepad->mapDpadDown->buttonMask : 0);
    }
    if ( pinDualDirLeft != (uint8_t)-1 ) {
        dualState |= (!gpio_get(pinDualDirLeft) ? gamepad->mapDpadLeft->buttonMask  : 0);
    }
    if ( pinDualDirRight != (uint8_t)-1 ) {
        dualState |= (!gpio_get(pinDualDirRight) ? gamepad->mapDpadRight->buttonMask : 0);
    }

    // Debounce our directional pins
    debounce();

    // Convert gamepad from process() output to uint8 value
    uint8_t gamepadState = gamepad->state.dpad;

    // Combined Mode
    if ( combineMode == DUAL_COMBINE_MODE_MIXED ) {
        SOCDDualClean(gamepad->options.socdMode); // Clean up Dual SOCD based on the mode

        // Second Input (Last Input Priority) needs to happen before we MPG clean
        if ( gamepad->options.socdMode == SOCD_MODE_SECOND_INPUT_PRIORITY ) {
            gamepadState = SOCDGamepadClean(gamepadState) | dualState;
        }
    }
    // Gamepad Overwrite Mode
    else if ( combineMode == DUAL_COMBINE_MODE_GAMEPAD ) {
        if ( gamepadState != 0 && (gamepadState != dualState)) {
            dualState = gamepadState;
        }
    }
    // Dual Overwrite Mode
    else if ( combineMode == DUAL_COMBINE_MODE_DUAL ) {
        if ( dualState != 0 && gamepadState != dualState) {
            gamepadState = dualState;
        }
    }
    
    gamepad->state.dpad = gamepadState;
}

void DualDirectionalInput::process()
{
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    uint8_t dualOut = dualState;

    // If we're in mixed mode
    if (combineMode == DUAL_COMBINE_MODE_MIXED) {
        uint8_t gamepadDpad = gpadToBinary(gamepad->options.dpadMode, gamepad->state);
        // Up-Win or Neutral Modify AFTER SOCD(gamepad), Last-Win Modifies BEFORE SOCD(gamepad)
        if ( gamepad->options.socdMode == SOCD_MODE_UP_PRIORITY ||
                gamepad->options.socdMode == SOCD_MODE_NEUTRAL ) {
            
            // Up-Win or Neutral: SOCD(gamepad) *already done* | SOCD(dual) *done in preprocess()*
            dualOut = SOCDCombine(gamepad->options.socdMode, gamepadDpad);
            
            // Modify Gamepad if we're in mixed Up-Win or Neutral and dual != gamepad
            if ( dualOut != gamepadDpad ) {
                OverrideGamepad(gamepad, gamepad->options.dpadMode, dualOut);
            }
        }
    } else { // We are not mixed mode, don't change dual output
        dualOut = dualState;
    }

    // Set Dual Directional Output
    OverrideGamepad(gamepad, dpadMode, dualOut);
}

void DualDirectionalInput::OverrideGamepad(Gamepad * gamepad, DpadMode mode, uint8_t dpad) {
    switch (mode) {
        case DPAD_MODE_LEFT_ANALOG:
            gamepad->state.lx = dpadToAnalogX(dpad);
            gamepad->state.ly = dpadToAnalogY(dpad);
            break;
        case DPAD_MODE_RIGHT_ANALOG:
            gamepad->state.rx = dpadToAnalogX(dpad);
            gamepad->state.ry = dpadToAnalogY(dpad);
            break;
        case DPAD_MODE_DIGITAL:
            gamepad->state.dpad = dpad;
            break;        
    }
}

uint8_t DualDirectionalInput::SOCDGamepadClean(uint8_t gamepadState) {
    // Gamepad SOCD Last-Win Clean
    switch (gamepadState & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
        case (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN): // If last state was Up or Down, exclude it from our gamepad
            gamepadState ^= (lastGPUD == DIRECTION_UP) ? GAMEPAD_MASK_UP : GAMEPAD_MASK_DOWN;
            break;
        case GAMEPAD_MASK_UP:
            gamepadState |= GAMEPAD_MASK_UP;
            lastGPUD = DIRECTION_UP;
            break;
        case GAMEPAD_MASK_DOWN:
            gamepadState |= GAMEPAD_MASK_DOWN;
            lastGPUD = DIRECTION_DOWN;
            break;
        default:
            lastGPUD = DIRECTION_NONE;
            break;
    }
    switch (gamepadState & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
        case (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT):
            if (lastGPLR != DIRECTION_NONE)
                gamepadState ^= (lastGPLR == DIRECTION_LEFT) ? GAMEPAD_MASK_LEFT : GAMEPAD_MASK_RIGHT;
            else
                lastGPLR = DIRECTION_NONE;
            break;
        case GAMEPAD_MASK_LEFT:
            gamepadState |= GAMEPAD_MASK_LEFT;
            lastGPLR = DIRECTION_LEFT;
            break;
        case GAMEPAD_MASK_RIGHT:
            gamepadState |= GAMEPAD_MASK_RIGHT;
            lastGPLR = DIRECTION_RIGHT;
            break;
        default:
            lastGPLR = DIRECTION_NONE;
            break;
    }
    return gamepadState;
}


uint8_t DualDirectionalInput::SOCDCombine(SOCDMode mode, uint8_t gamepadState) {
    uint8_t outState = dualState | gamepadState;
    switch (outState & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
        case (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN): // If last state was Up or Down, exclude it from our gamepad
            if ( mode == SOCD_MODE_NEUTRAL )
                outState ^= (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN);
            else if ( mode == SOCD_MODE_UP_PRIORITY )
                outState = GAMEPAD_MASK_UP;
            break;
        default:
            break;
    }
    switch (outState & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
        case (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT):
            outState ^= (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT);
            break;
        default:
            break;
    }

    return outState;
}

void DualDirectionalInput::SOCDDualClean(SOCDMode socdMode) {
    // Dual SOCD Last-Win Clean
    switch (dualState & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
        case (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN): // If last state was Up or Down, exclude it from our gamepad
            if ( socdMode == SOCD_MODE_UP_PRIORITY ) {
                dualState ^= GAMEPAD_MASK_DOWN; // Remove Down
				lastDualUD = DIRECTION_UP; // We're in UP mode
            } else if ( socdMode == SOCD_MODE_SECOND_INPUT_PRIORITY && lastDualUD != DIRECTION_NONE ) {
                dualState ^= (lastDualUD == DIRECTION_UP) ? GAMEPAD_MASK_UP : GAMEPAD_MASK_DOWN;
            } else {
				dualState ^= (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN); // Remove UP and Down in Neutral
                lastDualUD = DIRECTION_NONE;
            }
            break;
        case GAMEPAD_MASK_UP:
            lastDualUD = DIRECTION_UP;
            break;
        case GAMEPAD_MASK_DOWN:
            lastDualUD = DIRECTION_DOWN;
            break;
        default:
            lastDualUD = DIRECTION_NONE;
            break;
    }
    switch (dualState & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
        case (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT):
            if ( socdMode == SOCD_MODE_UP_PRIORITY || socdMode == SOCD_MODE_NEUTRAL ) {
                dualState ^= (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT); // Remove L + R to Neutral
                lastDualLR = DIRECTION_NONE;
            } else if ( socdMode == SOCD_MODE_SECOND_INPUT_PRIORITY ) {
                if (lastDualLR != DIRECTION_NONE)
                    dualState ^= (lastDualLR == DIRECTION_LEFT) ? GAMEPAD_MASK_LEFT : GAMEPAD_MASK_RIGHT; // Last Win
                else
                    lastDualLR = DIRECTION_NONE; 
            }
            break;
        case GAMEPAD_MASK_LEFT:
            lastDualLR = DIRECTION_LEFT;
            break;
        case GAMEPAD_MASK_RIGHT:
            lastDualLR = DIRECTION_RIGHT;
            break;
        default:
            lastDualLR = DIRECTION_NONE;
            break;
    }
}

uint8_t DualDirectionalInput::gpadToBinary(DpadMode dpadMode, GamepadState state) {
    uint8_t out = 0;
    switch(dpadMode) { // Convert gamepad to dual if we're in mixed
        case DPAD_MODE_DIGITAL:
            out = state.dpad;
            break;
        case DPAD_MODE_LEFT_ANALOG:
            if ( state.lx == GAMEPAD_JOYSTICK_MIN) {
                out = out | GAMEPAD_MASK_LEFT;
            } else if ( state.lx == GAMEPAD_JOYSTICK_MAX) {
                out = out | GAMEPAD_MASK_RIGHT;
            }
            if ( state.ly == GAMEPAD_JOYSTICK_MIN) {
                out = out | GAMEPAD_MASK_UP;
            } else if ( state.ly == GAMEPAD_JOYSTICK_MAX) {
                out = out | GAMEPAD_MASK_DOWN;
            }
            break;
        case DPAD_MODE_RIGHT_ANALOG:
            if ( state.rx == GAMEPAD_JOYSTICK_MIN) {
                out = out | GAMEPAD_MASK_LEFT;
            } else if ( state.rx == GAMEPAD_JOYSTICK_MAX) {
                out = out | GAMEPAD_MASK_RIGHT;
            }
            if ( state.ry == GAMEPAD_JOYSTICK_MIN) {
                out = out | GAMEPAD_MASK_UP;
            } else if ( state.ry == GAMEPAD_JOYSTICK_MAX) {
                out = out | GAMEPAD_MASK_DOWN;
            }
            break;
    }

    return out;
}
