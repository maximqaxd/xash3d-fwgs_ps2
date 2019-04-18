#pragma once
#ifndef VID_COMMON
#define VID_COMMON

typedef struct vidmode_s
{
	const char	*desc;
	int			width;
	int			height;
} vidmode_t;


typedef struct
{
	void*	context; // handle to GL rendering context
	int		safe;

	int		desktopBitsPixel;
	int		desktopWidth;
	int		desktopHeight;

	qboolean		initialized;	// OpenGL subsystem started
	qboolean		extended;		// extended context allows to GL_Debug
	qboolean        software;


} glwstate_t;

extern glwstate_t glw_state;

#define VID_MIN_HEIGHT 200
#define VID_MIN_WIDTH 320

extern convar_t	*vid_fullscreen;
extern convar_t	*vid_displayfrequency;
extern convar_t	*vid_highdpi;
extern convar_t	*gl_wgl_msaa_samples;
void R_SaveVideoMode( int w, int h );
void VID_CheckChanges( void );
const char *VID_GetModeString( int vid_mode );
void VID_StartupGamma( void );
void GL_SwapBuffers();

#endif // VID_COMMON
