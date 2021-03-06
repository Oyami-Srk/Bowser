
#ifndef COLORLABEL_H
#define COLORLABEL_H

// Yanked this from Be's Website.
// It's from their Modular ColorPicker Newsletter article
// Slight modifications for animation, and didn't do as
// modular a color picker

#include <Window.h>
#include <Control.h>
//#include <Messenger.h>
//#include <String.h>

#include "IRCDefines.h"

class BColorControl;

class ColorLabel : public BControl
{
	rgb_color			color;
	rgb_color			alpha;

	bool					tracking;
	bigtime_t			mousedown;
	rgb_color			saveColor;

	public:
							ColorLabel (
								BRect,
								const char *,
								const char *,
								rgb_color,
								BMessage *,
								uint32 = B_FOLLOW_LEFT | B_FOLLOW_TOP,
								uint32 = B_WILL_DRAW | B_NAVIGABLE);

	virtual				~ColorLabel (void);

	virtual void		AttachedToWindow (void);
	virtual void		Draw (BRect);
	virtual void		MessageReceived (BMessage *);
	virtual void		MouseDown (BPoint);
	virtual void		MouseUp (BPoint);
	virtual status_t	Invoke (BMessage * = 0);

	virtual void		GetPreferredSize (float *, float *);
	virtual void		ResizeToPreferred (void);

	rgb_color			ValueAsColor (void) const;
	virtual void		SetColor (rgb_color);

	BRect					ColorRect (void) const;

	private:
	rgb_color			ShiftColor (rgb_color, float) const;
	rgb_color			Inverted (void) const;
};

class ColorPicker : public BWindow
{
	BColorControl		*picker;

	BMessenger			audience;
	BMessage				invoked;

	public:
							ColorPicker (
								BPoint,
								rgb_color,
								BMessenger,
								BMessage *);

	virtual				~ColorPicker (void);
	virtual void		MessageReceived (BMessage *);
};

#endif
