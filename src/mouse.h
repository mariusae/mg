/*
 * Mouse support for mg.
 * This file is in the public domain.
 */

#ifndef MOUSE_H
#define MOUSE_H

/* Mouse button definitions */
#define MOUSE_BUTTON_LEFT	0
#define MOUSE_BUTTON_MIDDLE	1
#define MOUSE_BUTTON_RIGHT	2
#define MOUSE_BUTTON_RELEASE	3
#define MOUSE_WHEEL_UP		64
#define MOUSE_WHEEL_DOWN	65

/* Mouse event types */
#define MOUSE_PRESS		0
#define MOUSE_RELEASE		1
#define MOUSE_DRAG		2

/* Mouse event structure */
struct mouse_event {
	int	me_type;	/* MOUSE_PRESS, MOUSE_RELEASE, MOUSE_DRAG */
	int	me_button;	/* Which button */
	int	me_x;		/* Column (0-based) */
	int	me_y;		/* Row (0-based) */
};

/* Function prototypes */
void	mouse_init(void);
void	mouse_close(void);
int	mouse_parse(int, struct mouse_event *);
int	mouse_handle(struct mouse_event *);

/* Global mouse state */
extern int mouse_enabled;

#endif /* MOUSE_H */
