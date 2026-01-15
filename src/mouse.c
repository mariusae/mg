/*
 * Mouse support for mg.
 * This file is in the public domain.
 *
 * Implements mouse navigation and selection similar to neovim:
 * - Click to position cursor
 * - Click and drag to select text
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "def.h"
#include "mouse.h"

int mouse_enabled = 0;

/* Track mouse state for drag operations */
static int mouse_down = 0;
static int drag_start_x = -1;
static int drag_start_y = -1;

/* Track double-click detection */
static struct timeval last_click_time = {0, 0};
static int last_click_x = -1;
static int last_click_y = -1;

#define DOUBLE_CLICK_MS 400	/* Double-click time threshold in milliseconds */

/* Escape sequences for mouse mode */
#define MOUSE_SGR_ON	"\033[?1000h\033[?1002h\033[?1006h"
#define MOUSE_SGR_OFF	"\033[?1006l\033[?1002l\033[?1000l"

/*
 * Scroll the view by n lines without moving the cursor.
 * Positive n scrolls down (view moves forward), negative scrolls up.
 * This preserves selections during mouse wheel scrolling.
 */
static int
scroll_view_only(int n)
{
	struct line *lp;
	int i;

	if (n == 0)
		return TRUE;

	lp = curwp->w_linep;

	if (n > 0) {
		/* Scroll down - move view forward */
		for (i = 0; i < n; i++) {
			if (lforw(lp) == curbp->b_headp)
				break;
			lp = lforw(lp);
		}
	} else {
		/* Scroll up - move view backward */
		for (i = 0; i > n; i--) {
			if (lback(lp) == curbp->b_headp)
				break;
			lp = lback(lp);
		}
	}

	if (lp != curwp->w_linep) {
		curwp->w_linep = lp;
		curwp->w_rflag |= WFFULL | WFSAVE;
	}

	return TRUE;
}

/*
 * Enable mouse tracking in the terminal.
 * Uses SGR extended mouse mode (1006) for better coordinate support.
 */
void
mouse_init(void)
{
	fputs(MOUSE_SGR_ON, stdout);
	fflush(stdout);
	mouse_enabled = 1;
	mouse_down = 0;
}

/*
 * Disable mouse tracking.
 */
void
mouse_close(void)
{
	if (mouse_enabled) {
		fputs(MOUSE_SGR_OFF, stdout);
		fflush(stdout);
		mouse_enabled = 0;
	}
}

/*
 * Parse SGR mouse sequence.
 * Format: CSI < button ; x ; y M (press) or CSI < button ; x ; y m (release)
 * Returns 1 if successfully parsed, 0 otherwise.
 * The first character (after ESC [) has already been consumed.
 */
int
mouse_parse(int startc, struct mouse_event *mep)
{
	int c;
	int button = 0, x = 0, y = 0;
	int released = 0;

	/* startc should be '<' for SGR mode */
	if (startc != '<')
		return 0;

	/* Read button number */
	while ((c = ttgetc()) >= '0' && c <= '9')
		button = button * 10 + (c - '0');

	if (c != ';')
		return 0;

	/* Read X coordinate */
	while ((c = ttgetc()) >= '0' && c <= '9')
		x = x * 10 + (c - '0');

	if (c != ';')
		return 0;

	/* Read Y coordinate */
	while ((c = ttgetc()) >= '0' && c <= '9')
		y = y * 10 + (c - '0');

	/* Check terminator: 'M' = press, 'm' = release */
	if (c == 'm')
		released = 1;
	else if (c != 'M')
		return 0;

	/* Convert to 0-based coordinates */
	x--;
	y--;

	/* Determine event type */
	if (button & 32) {
		/* Motion with button held (drag) */
		mep->me_type = MOUSE_DRAG;
		mep->me_button = button & ~32;
	} else if (released) {
		mep->me_type = MOUSE_RELEASE;
		mep->me_button = button;
	} else {
		mep->me_type = MOUSE_PRESS;
		mep->me_button = button;
	}

	mep->me_x = x;
	mep->me_y = y;

	return 1;
}

/*
 * Find the window at screen row y.
 * Returns NULL if no window found (e.g., echo area).
 */
static struct mgwin *
window_at_row(int row)
{
	struct mgwin *wp;

	for (wp = wheadp; wp != NULL; wp = wp->w_wndp) {
		/* Check if row is within this window's text area */
		if (row >= wp->w_toprow && row < wp->w_toprow + wp->w_ntrows)
			return wp;
	}
	return NULL;
}

/*
 * Convert screen column to buffer offset within a line.
 * Handles tabs and control characters properly.
 */
static int
col_to_offset(struct line *lp, int targetcol, int tabw)
{
	int col = 0;
	int i;

	for (i = 0; i < llength(lp); i++) {
		int c = lgetc(lp, i);

		if (col >= targetcol)
			return i;

		if (c == '\t') {
			col = ntabstop(col, tabw);
		} else if (ISCTRL(c)) {
			col += 2;
		} else {
			col++;
		}
	}

	/* Past end of line */
	return llength(lp);
}

/*
 * Move cursor to the screen position (x, y).
 * Returns TRUE if successful, FALSE otherwise.
 */
static int
mouse_move_to(int x, int y)
{
	struct mgwin *wp;
	struct line *lp;
	int row, line_count;

	/* Find window at this row */
	wp = window_at_row(y);
	if (wp == NULL)
		return FALSE;

	/* Switch to this window if necessary */
	if (wp != curwp) {
		curwp = wp;
		curbp = wp->w_bufp;
	}

	/* Calculate which line in the buffer */
	row = y - wp->w_toprow;
	lp = wp->w_linep;
	line_count = 0;

	/* Walk down to the target line */
	while (line_count < row && lp != curbp->b_headp) {
		lp = lforw(lp);
		line_count++;
	}

	/* Don't go past the end of the buffer */
	if (lp == curbp->b_headp)
		lp = lback(lp);

	/* Update window line number (calculate from top of buffer) */
	{
		struct line *tlp = bfirstlp(curbp);
		int lineno = 1;
		while (tlp != lp && tlp != curbp->b_headp) {
			tlp = lforw(tlp);
			lineno++;
		}
		curwp->w_dotline = lineno;
	}

	/* Set cursor position */
	curwp->w_dotp = lp;
	curwp->w_doto = col_to_offset(lp, x, curbp->b_tabw);
	curwp->w_rflag |= WFMOVE;

	return TRUE;
}

/*
 * Select the word at the current cursor position.
 * Sets mark at start of word and moves cursor to end of word.
 * Returns TRUE if a word was selected, FALSE if cursor is not on a word.
 */
static int
select_word(void)
{
	struct line *lp = curwp->w_dotp;
	int offset = curwp->w_doto;

	/* Check if we're on a word character */
	if (offset >= llength(lp) || !ISWORD(lgetc(lp, offset)))
		return FALSE;

	/* Move to start of word */
	while (offset > 0 && ISWORD(lgetc(lp, offset - 1)))
		offset--;

	/* Set cursor at start of word, then set mark */
	curwp->w_doto = offset;
	isetmark();

	/* Move to end of word */
	while (offset < llength(lp) && ISWORD(lgetc(lp, offset)))
		offset++;

	/* Set cursor at end of word */
	curwp->w_doto = offset;
	curwp->w_rflag |= WFFULL;

	return TRUE;
}

/*
 * Handle a mouse event.
 * Returns TRUE if the event was handled, FALSE otherwise.
 */
int
mouse_handle(struct mouse_event *mep)
{
	switch (mep->me_type) {
	case MOUSE_PRESS:
		if (mep->me_button == MOUSE_BUTTON_LEFT) {
			struct timeval now;
			long elapsed_ms;
			int is_double_click = 0;

			gettimeofday(&now, NULL);

			/* Calculate elapsed time in milliseconds */
			elapsed_ms = (now.tv_sec - last_click_time.tv_sec) * 1000 +
			             (now.tv_usec - last_click_time.tv_usec) / 1000;

			/* Check for double-click: same position within time threshold */
			if (elapsed_ms <= DOUBLE_CLICK_MS && elapsed_ms >= 0 &&
			    mep->me_x == last_click_x && mep->me_y == last_click_y) {
				is_double_click = 1;
			}

			/* Update last click tracking */
			last_click_time = now;
			last_click_x = mep->me_x;
			last_click_y = mep->me_y;

			/* Left click - position cursor */
			mouse_down = 1;
			drag_start_x = mep->me_x;
			drag_start_y = mep->me_y;

			/* Clear any existing mark and force redraw */
			if (curwp->w_markp != NULL) {
				curwp->w_markp = NULL;
				curwp->w_marko = 0;
				curwp->w_markline = 0;
				curwp->w_rflag |= WFFULL;
			}

			/* Position cursor first */
			if (mouse_move_to(mep->me_x, mep->me_y) == FALSE)
				return FALSE;

			/* If double-click, select the word at cursor */
			if (is_double_click)
				select_word();

			return TRUE;
		} else if (mep->me_button == MOUSE_WHEEL_UP) {
			/* Scroll up - view only, preserve cursor/selection */
			return scroll_view_only(-3);
		} else if (mep->me_button == MOUSE_WHEEL_DOWN) {
			/* Scroll down - view only, preserve cursor/selection */
			return scroll_view_only(3);
		}
		break;

	case MOUSE_DRAG:
		if (mouse_down && mep->me_button == MOUSE_BUTTON_LEFT) {
			/* Set mark at drag start if not already set */
			if (curwp->w_markp == NULL) {
				/* Save current position as mark */
				isetmark();
			}
			/* Move cursor to current drag position */
			return mouse_move_to(mep->me_x, mep->me_y);
		}
		break;

	case MOUSE_RELEASE:
		if (mep->me_button == MOUSE_BUTTON_LEFT) {
			mouse_down = 0;
			/* If we have a selection, copy to system clipboard */
			if (curwp->w_markp != NULL)
				region_to_clipboard();
			return TRUE;
		}
		break;
	}

	return FALSE;
}
