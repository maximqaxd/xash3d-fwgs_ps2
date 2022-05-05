/*
gu_image.c - texture uploading and processing
Copyright (C) 2010 Uncle Mike
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <malloc.h>
#include <pspsuspend.h>
#include "vram_psp.h"
#include "gu_local.h"
#include "crclib.h"

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

static gl_texture_t		gl_textures[MAX_TEXTURES];
static gl_texture_t*	gl_texturesHashTable[TEXTURES_HASH_SIZE];
static uint		gl_numTextures;

/*
	num_colors = PALETTE_BLOCKS x 16 (16-bit CLUT)
	num_colors = PALETTE_BLOCKS x 8 (32-bit CLUT)
*/
#define PALETTE_FORMAT GU_PSM_4444
#if (PALETTE_FORMAT == GU_PSM_4444) || (PALETTE_FORMAT == GU_PSM_5551) || (PALETTE_FORMAT == GU_PSM_5650)
#define PALETTE_SIZE   2 * 256
#define PALETTE_BLOCKS 16
#elif PALETTE_FORMAT == GU_PSM_8888
#define PALETTE_SIZE   4 * 256
#define PALETTE_BLOCKS 32
#else
#error Current PALETTE_FORMAT not supported!
#endif

#define TEXTURE_SIZE_MIN 16 //8

#define IsLightMap( tex )	( FBitSet(( tex )->flags, TF_ATLAS_PAGE ))

/*
=================
R_GetTexture

acess to array elem
=================
*/
gl_texture_t *R_GetTexture( GLenum texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return &gl_textures[texnum];
}

/*
=================
GL_Bind
=================
*/
void GL_Bind( GLint tmu, GLenum texnum )
{
	gl_texture_t	*texture;
	int				i, offset, width, height;

	// missed or invalid texture?
	if( texnum <= 0 || texnum >= MAX_TEXTURES )
	{
		if( texnum != 0 )
			gEngfuncs.Con_DPrintf( S_ERROR "GL_Bind: invalid texturenum %d\n", texnum );
		texnum = tr.defaultTexture;
	}

	if( glState.currentTexture == texnum )
		return;
	
	texture = &gl_textures[texnum];

	// Set palette
	if( texture->format == GU_PSM_T8 && texture->dstPalette )
	{
		sceGuClutMode( PALETTE_FORMAT, 0, 0xff, 0 );
		sceGuClutLoad( PALETTE_BLOCKS, texture->dstPalette );
	}

	// Set texture parameters
	sceGuTexMode( texture->format, texture->numMips - 1, 0, FBitSet( texture->guFlags, GUFLAGTEXSWIZZLED ) ? GU_TRUE : GU_FALSE );
	if( texture->numMips > 1 )
	{
		sceGuTexFilter( GU_LINEAR_MIPMAP_LINEAR, GU_LINEAR_MIPMAP_LINEAR );
		sceGuTexLevelMode( gl_texture_lodfunc->value, gl_texture_lodbias->value );
		sceGuTexSlope( gl_texture_lodslope->value );
	}
	else
	{
		sceGuTexFilter( GU_LINEAR, GU_LINEAR );
	}

	// Set base texture
	sceGuTexImage( 0, texture->width, texture->height, texture->width, texture->dstTexture );

	// Set mip textures
	if( texture->numMips > 1 )
	{
		offset = texture->width * texture->height;
		for ( i = 1; i < texture->numMips; i++ )
		{
			width = Q_max( TEXTURE_SIZE_MIN, ( texture->width >> i ) );
			height = Q_max( TEXTURE_SIZE_MIN, ( texture->height >> i ) );
			sceGuTexImage( i, width, height, width,  &texture->dstTexture[offset] );
			offset += width * height;
		}
	}
	glState.currentTexture = texnum;
}

/*
=================
GL_ApplyTextureParams
=================
*/
void GL_ApplyTextureParams( gl_texture_t *tex )
{
#if 0
	vec4_t	border = { 0.0f, 0.0f, 0.0f, 1.0f };

	if( !glw_state.initialized )
		return;

	Assert( tex != NULL );

	// set texture filter
	if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( !FBitSet( tex->flags, TF_NOCOMPARE ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB );
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_FUNC_ARB, GL_LEQUAL );
		}

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE );
		else pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_INTENSITY );

		if( FBitSet( tex->flags, TF_NEAREST ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// allow max anisotropy as 1.0f on depth textures
		if( GL_Support( GL_ANISOTROPY_EXT ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f );
	}
	else if( FBitSet( tex->flags, TF_NOMIPMAP ) || tex->numMips <= 1 )
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || ( IsLightMap( tex ) && gl_lightmap_nearest->value ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}
	else
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// set texture anisotropy if available
		if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_ALPHACONTRAST ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

		// set texture LOD bias if available
		if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ))
			pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );
	}

	// check if border is not supported
	if( FBitSet( tex->flags, TF_BORDER ) && !GL_Support( GL_CLAMP_TEXBORDER_EXT ))
	{
		ClearBits( tex->flags, TF_BORDER );
		SetBits( tex->flags, TF_CLAMP );
	}

	// only seamless cubemaps allows wrap 'clamp_to_border"
	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB && !GL_Support( GL_ARB_SEAMLESS_CUBEMAP ) && FBitSet( tex->flags, TF_BORDER ))
		ClearBits( tex->flags, TF_BORDER );

	// set texture wrap
	if( FBitSet( tex->flags, TF_BORDER ))
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER );

		pglTexParameterfv( tex->target, GL_TEXTURE_BORDER_COLOR, border );
	}
	else if( FBitSet( tex->flags, TF_CLAMP ))
	{
		if( GL_Support( GL_CLAMPTOEDGE_EXT ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP );
		}
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_REPEAT );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_REPEAT );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_REPEAT );
	}
#endif
}

/*
=================
GL_UpdateTextureParams
=================
*/
static void GL_UpdateTextureParams( int iTexture )
{
#if 0
	gl_texture_t	*tex = &gl_textures[iTexture];

	Assert( tex != NULL );

	if( !tex->texnum ) return; // free slot

	GL_Bind( XASH_TEXTURE0, iTexture );

	// set texture anisotropy if available
	if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP|TF_ALPHACONTRAST ))
		pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

	// set texture LOD bias if available
	if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP ))
		pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );

	if( IsLightMap( tex ))
	{
		if( gl_lightmap_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}

	if( tex->numMips <= 1 ) return;

	if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
#endif
}

/*
=================
R_SetTextureParameters
=================
*/
void R_SetTextureParameters( void )
{
#if 0
	int	i;

	if( GL_Support( GL_ANISOTROPY_EXT ))
	{
		if( gl_texture_anisotropy->value > glConfig.max_texture_anisotropy )
			gEngfuncs.Cvar_SetValue( "gl_anisotropy", glConfig.max_texture_anisotropy );
		else if( gl_texture_anisotropy->value < 1.0f )
			gEngfuncs.Cvar_SetValue( "gl_anisotropy", 1.0f );
	}

	if( GL_Support( GL_TEXTURE_LOD_BIAS ))
	{
		if( gl_texture_lodbias->value < -glConfig.max_texture_lod_bias )
			gEngfuncs.Cvar_SetValue( "gl_texture_lodbias", -glConfig.max_texture_lod_bias );
		else if( gl_texture_lodbias->value > glConfig.max_texture_lod_bias )
			gEngfuncs.Cvar_SetValue( "gl_texture_lodbias", glConfig.max_texture_lod_bias );
	}

	ClearBits( gl_texture_anisotropy->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_lodbias->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_nearest->flags, FCVAR_CHANGED );
	ClearBits( gl_lightmap_nearest->flags, FCVAR_CHANGED );

	// change all the existing mipmapped texture objects
	for( i = 0; i < gl_numTextures; i++ )
		GL_UpdateTextureParams( i );
#endif
}

/*
==================
GL_CalcImageSize
==================
*/
static size_t GL_CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
	case PF_LUMINANCE:
		size = width * height * depth;
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	}

	return size;
}

/*
==================
GL_CalcTextureSize
==================
*/
static size_t GL_CalcTextureSize( int format, int width, int height )
{
	size_t	size = 0;

	switch( format )
	{
	case GU_PSM_T4:
	case GU_PSM_DXT1:
		size = ( ( width * height ) >> 1 );
		break;
	case GU_PSM_T8:
	case GU_PSM_DXT3:
	case GU_PSM_DXT5:
		size = width * height;
		break;
	case GU_PSM_T16:
	case GU_PSM_4444:
	case GU_PSM_5551:
	case GU_PSM_5650:
		size = width * height * 2;
		break;
	case GU_PSM_T32:
	case GU_PSM_8888:
		size = width * height * 4;
		break;
	default:
		gEngfuncs.Host_Error( "GL_CalcTextureSize: bad texture internal format (%u)\n", format );
		break;
	}
	return size;
}

static int GL_CalcMipmapCount( gl_texture_t *tex, qboolean haveBuffer, size_t *mipsize )
{
	int	width, height;
	int	mipcount;

	Assert( tex != NULL );

	if( !haveBuffer )
		return 1;

#if 0 
	// brush model only
	if( !FBitSet( tex->flags, TF_ALLOW_EMBOSS ) )
		return 1;
#endif

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ) )
		return 1;

	// 8 levels - 7 + 1 base
	for( mipcount = 1; mipcount < 8; mipcount++ )
	{
		width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> mipcount ) );
		height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> mipcount ) );

		if( width == TEXTURE_SIZE_MIN && height == TEXTURE_SIZE_MIN )
			break;

		// calc without base size
		if( mipsize != NULL )
			*mipsize += GL_CalcTextureSize( tex->format, width, height );
	}
	return mipcount;
}

/*
================
GL_SetTextureDimensions
================
*/
static void GL_SetTextureDimensions( gl_texture_t *tex, int width, int height )
{
	int	maxTextureSize;
	int stepWidth, stepHeight;

	Assert( tex != NULL );
	
	maxTextureSize = glConfig.max_texture_size;

	// store original sizes
	tex->srcWidth = width;
	tex->srcHeight = height;
	
#if 0 // scale up
	stepWidth = ( int )ceilf( logf( width  ) * 1.0f / logf( 2.0f ) );
	stepHeight = ( int )ceilf( logf( height ) * 1.0f / logf( 2.0f ) );
#else // scale down
	stepWidth = ( int )floorf( logf( width  ) * 1.0f / logf( 2.0f ) );
	stepHeight = ( int )floorf( logf( height ) * 1.0f / logf( 2.0f ) );
#endif
	width  = ( 1 << stepWidth );
	height = ( 1 << stepHeight );

	if( width > maxTextureSize || height > maxTextureSize )
	{
		while( width > maxTextureSize || height > maxTextureSize )
		{
			width >>= 1;
			height >>= 1;
		}
	}

	// set the texture dimensions
	tex->width  = Q_max( TEXTURE_SIZE_MIN, width );
	tex->height = Q_max( TEXTURE_SIZE_MIN, height );
}

/*
===============
GL_SetTextureFormat
===============
*/
static void GL_SetTextureFormat( gl_texture_t *tex, pixformat_t format, int channelMask )
{
	qboolean	haveColor = ( channelMask & IMAGE_HAS_COLOR );
	qboolean	haveAlpha = ( channelMask & IMAGE_HAS_ALPHA );

	Assert( tex != NULL );

	if( ImageDXT( format ) )
	{
		switch( format )
		{
		case PF_DXT1: tex->format = GU_PSM_DXT1; break;	// never use DXT1 with 1-bit alpha
		case PF_DXT3: tex->format = GU_PSM_DXT3; break;
		case PF_DXT5: tex->format = GU_PSM_DXT5; break;
		}
	}
	/*
	else if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( FBitSet( tex->flags, TF_ARB_16BIT ))
			tex->format = GL_DEPTH_COMPONENT16;
		else if( FBitSet( tex->flags, TF_ARB_FLOAT ))
			tex->format = GL_DEPTH_COMPONENT32F;
		else tex->format = GL_DEPTH_COMPONENT24;
	}*/
	else if( ImageIND( format ) )
	{
		tex->format = GU_PSM_T8;
	}
	else
	{
		if ( IsLightMap( tex ) )
			tex->format = GU_PSM_5650;
		else if( haveAlpha )
			tex->format = GU_PSM_4444;
		else
			tex->format = GU_PSM_5650;
	}
}

/*
=================
GL_ResampleTexture32

Assume input buffer is RGBA
=================
*/
void GL_ResampleTexture32( const byte *source, int inWidth, int inHeight, byte *dest, int outWidth, int outHeight, qboolean isNormalMap )
{
	uint		frac, fracStep;
	uint		*in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte		*pix1, *pix2, *pix3, *pix4;
	uint		*out, *inRow1, *inRow2;
	vec3_t		normal;
	int		i, x, y;

	if( !source )
		return;

	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)dest;

	frac = fracStep >> 2;
	for( i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				normal[0] = MAKE_SIGNED( pix1[0] ) + MAKE_SIGNED( pix2[0] ) + MAKE_SIGNED( pix3[0] ) + MAKE_SIGNED( pix4[0] );
				normal[1] = MAKE_SIGNED( pix1[1] ) + MAKE_SIGNED( pix2[1] ) + MAKE_SIGNED( pix3[1] ) + MAKE_SIGNED( pix4[1] );
				normal[2] = MAKE_SIGNED( pix1[2] ) + MAKE_SIGNED( pix2[2] ) + MAKE_SIGNED( pix3[2] ) + MAKE_SIGNED( pix4[2] );

				if( !VectorNormalizeLength( normal ))
					VectorSet( normal, 0.5f, 0.5f, 1.0f );

				((byte *)(out+x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out+x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out+x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out+x))[3] = 255;
			}
		}
	}
	else
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}
}

/*
=================
GL_ResampleTexture8

For indexed textures
=================
*/
void GL_ResampleTexture8(const byte *source, int inWidth, int inHeight, byte *dest, int outWidth, int outHeight)
{
	uint		fracstep;
	byte		*out;
	const byte	*inrow;
	uint		frac;
	int			i, j;

	if( !source )
		return;

	fracstep = inWidth * 0x10000 / outWidth;
	out = dest;

	for (i = 0; i < outHeight ; ++i, out += outWidth)
	{
		inrow	= source + inWidth * (i * inHeight / outHeight);
		frac	= fracstep >> 1;
		for (j = 0; j < outWidth; ++j, frac += fracstep)
		{
			out[j] = inrow[frac >> 16];
		}
	}
}
/*
=================
GL_BoxFilter3x3

box filter 3x3
=================
*/
void GL_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int		r = 0, g = 0, b = 0, a = 0;
	int		count = 0, acount = 0;
	int		i, j, u, v;
	const byte	*pixel;

	for( i = 0; i < 3; i++ )
	{
		u = ( i - 1 ) + x;

		for( j = 0; j < 3; j++ )
		{
			v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					a += pixel[3];
					acount++;
				}
			}
		}
	}

	if(  acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
//	out[3] = (int)( SimpleSpline( ( a / 12.0f ) / 255.0f ) * 255 );
}

/*
=================
GL_ApplyFilter

Apply box-filter to 1-bit alpha
=================
*/
byte *GL_ApplyFilter( const byte *source, int width, int height )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) )
		return in;

	for( i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			GL_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return out;
}

/*
=================
GL_BuildMipMap

Operates in place, quartering the size of the texture
=================
*/
static void GL_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	byte	*out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	mipHeight = Q_max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;
	row = srcWidth << 2;

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

/*
===============
GL_PixelConverter
===============
*/
static void GL_PixelConverter( byte *dst, const byte *src, int size, qboolean alpha, int format )
{
#if 0 // undone
	if(alpha)
	{
		__asm__ volatile (
			".set		push\n"					// save assembler option
			".set		noreorder\n"			// suppress reordering
			"move		$8,   %0\n"				// $8 = &dst[0]
			"move		$9,   %1\n"				// $9 = &src[0]
			"move		$10,  %2\n"				// $10 = size
		"0:\n"

			"ulv.q		C000, 0($8)\n"			// Load 4 pixels from src

			"vt4444.q	C010, C000\n"			// Convert 4 -> 2

			"sv.s		S010, 0($9)\n"
			"sv.s		S010, 8($9)\n"
			"addiu		$8,  $8,  16\n"			// $8 = $8 + 16
			"addiu		$9,  $9,  8\n"			// $9 = $9 + 8

 			"bne		$10,  $8,   0b\n"		// if ( $11 != $8 ) jump to loop
			"addiu		$10,  $10,  -4\n"		// $10 = $10 - 4						( delay slot )
			".set		pop\n"					// Restore assembler option
			:	"r"( dst ), "r"( src ), "r"( size ), "r"( format )
			:	"$8", "$9", "$10"
		);
	}
	else
	{

	}
#else
	int	i;
	byte color[4];

	for( i = 0; i < size; i++ )
	{
		switch( format )
		{
		case GU_PSM_4444:
			color[0] = *src; src++;
			color[1] = *src; src++;
			color[2] = *src; src++;
			color[3] = alpha ? *src : 0xff; if( alpha ) src++;
			*dst  = ( color[0] >> 4 ) & 0x0f;
			*dst |= ( color[1]      ) & 0xf0; dst++;
			*dst  = ( color[2] >> 4 ) & 0x0f;
			*dst |= ( color[3]      ) & 0xf0; dst++;
			break;
		case GU_PSM_5551:
			color[0] = ( *src >> 3 ) & 0x1f; src++;
			color[1] = ( *src >> 3 ) & 0x1f; src++;
			color[2] = ( *src >> 3 ) & 0x1f; src++;
			color[3] = alpha ? ( ( *src >> 7 ) & 0x01 ) : 0xff; if( alpha ) src++;
			*dst  = color[0];
			*dst |= ( color[1] << 5 ) & 0xe0;  dst++;
			*dst  = ( color[1] >> 3 ) & 0x03;
			*dst |= ( color[2] << 2 ) & 0x7c;
			*dst |= ( color[3] << 7 ) & 0x80;  dst++;
			break;
		case GU_PSM_5650:
			color[0] = ( *src >> 3 ) & 0x1f; src++;
			color[1] = ( *src >> 2 ) & 0x3f; src++;
			color[2] = ( *src >> 3 ) & 0x1f; src++; if( alpha ) src++;
			*dst  = color[0];
			*dst |= ( color[1] << 5 ) & 0xe0; dst++;
			*dst  = ( color[1] >> 3 ) & 0x07;
			*dst |= ( color[2] << 3 ) & 0xf8; dst++;
			break;
		case GU_PSM_8888:
			*dst = *src; src++; dst++;
			*dst = *src; src++; dst++;
			*dst = *src; src++; dst++;
			*dst = alpha ? *src : 0xff; if( alpha ) src++; dst++;
			break;
		default:
			break;
		}
	}
#endif
}
/*
===============
GL_TextureSwizzle

fast swizzling
===============
*/
static void GL_TextureSwizzle( byte *dst, const byte *src, uint width, uint height )
{
	uint	blockx, blocky;
	uint	j;
	uint	width_blocks = ( width / 16 );
	uint	height_blocks = ( height / 8 );
	uint	src_pitch = ( width - 16 ) / 4;
	uint	src_row = width * 8;
	const byte*	ysrc = src;
	uint*	dst32 = ( uint* )dst;

	for ( blocky = 0; blocky < height_blocks; ++blocky )
	{
		const byte* xsrc = ysrc;
		for ( blockx = 0; blockx < width_blocks; ++blockx )
		{
			const int* src32 = ( uint* )xsrc;
			for ( j = 0; j < 8; ++j )
			{
				*( dst32++ ) = *( src32++ );
				*( dst32++ ) = *( src32++ );
				*( dst32++ ) = *( src32++ );
				*( dst32++ ) = *( src32++ );
				src32 += src_pitch;
			}
			xsrc += 16;
		}
		ysrc += src_row;
	}
}

/*
===============
GL_UploadTexture

upload texture into memory
===============
*/
static qboolean GL_UploadTexture( gl_texture_t *tex, rgbdata_t *pic )
{
	size_t			texSize;
	uint			width, height;
	uint			i;
	uint			offset;
	qboolean		normalMap;
	void			*volBuff;
	unsigned int	volSize;
	int 			volUid;

	// dedicated server
	if( !glw_state.initialized )
		return true;

	Assert( pic != NULL );
	Assert( tex != NULL );

	GL_SetTextureDimensions( tex, pic->width, pic->height );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	if(( pic->width * pic->height ) & 3 )
	{
		// will be resampled, just tell me for debug targets
		gEngfuncs.Con_Reportf( "GL_UploadTexture: %s s&3 [%d x %d]\n", tex->name, pic->width, pic->height );
	}

	if( !pic->buffer )
		return true;
	
	// Prepare sizes
	offset = GL_CalcImageSize( pic->type, pic->width, pic->height, /* pic->depth */ 1 );
	texSize = GL_CalcTextureSize( tex->format, tex->width, tex->height );
	normalMap = FBitSet( tex->flags, TF_NORMALMAP ) ? true : false;
	tex->numMips = ImageIND( pic->type ) ? GL_CalcMipmapCount( tex, ( pic->buffer != NULL ), &texSize) : 1;

	// Volatile memory for temporary buffer 
	volUid = sceKernelVolatileMemLock( 0, &volBuff, &volSize );
	if( volUid != 0)
		gEngfuncs.Host_Error( "GL_AllocTexture: volatile memory lock error 0x%08x ! \n", volUid);

	// Check allocation size
	if( tex->dstTexture && ( tex->size != texSize ) )
	{
		if( FBitSet( tex->guFlags, GUFLAGTEXINVRAM ) )
			vfree( tex->dstTexture );
		else
			free( tex->dstTexture );
		
		tex->dstTexture = NULL;
		ClearBits( tex->guFlags, GUFLAGTEXINVRAM );
	}

	// already allocated?
	if( !tex->dstTexture )
	{
		tex->dstTexture = ( byte* )valloc( texSize );
		if( !tex->dstTexture )
		{
			tex->dstTexture = ( byte* )memalign( 16, texSize );
			if ( !tex->dstTexture )
			{
				sceKernelVolatileMemUnlock( 0 );
				gEngfuncs.Host_Error( "GL_AllocTexture: out of memory! ( texture: %lu %s )\n", texSize, tex->name);
			}
		}
		else
		{
			SetBits( tex->guFlags, GUFLAGTEXINVRAM );
		}
	}

	if( ImageDXT( pic->type ) )
	{
		memcpy( tex->dstTexture, pic->buffer, texSize );
	}
	else if( ImageIND( pic->type ) )
	{
		if( !tex->dstPalette )
		{
			tex->dstPalette = ( byte* )memalign( 16, PALETTE_SIZE );
			if ( !tex->dstPalette )
			{
				sceKernelVolatileMemUnlock( 0 );
				gEngfuncs.Host_Error( "GL_AllocTexture: out of memory! ( palette: %s )\n", tex->name);
			}
		}

		// Load palette
		GL_PixelConverter( tex->dstPalette, pic->palette, 256, (pic->type == PF_INDEXED_32) ? true : false, PALETTE_FORMAT );
		sceKernelDcacheWritebackRange( tex->dstPalette, PALETTE_SIZE );

		// Load base + mip texture
		int mip_offset = 0;
		for( i = 0; i < tex->numMips; i++ )
		{
			width = Q_max( TEXTURE_SIZE_MIN, ( tex->width >> i ) );
			height = Q_max( TEXTURE_SIZE_MIN, ( tex->height >> i ) );

			if( ( pic->width != width ) || ( pic->height != height ) )
				GL_ResampleTexture8( pic->buffer, pic->width, pic->height, volBuff, width, height );
			else memcpy(volBuff, pic->buffer, offset);

			GL_TextureSwizzle( tex->dstTexture + mip_offset, volBuff, width, height );
			mip_offset += GL_CalcTextureSize( tex->format, width, height );
		}
		SetBits( tex->guFlags, GUFLAGTEXSWIZZLED );
	}
	else // RGBA32
	{
		if( ( pic->width != tex->width ) || ( pic->height != tex->height ) )
		{
			GL_ResampleTexture32( pic->buffer, pic->width, pic->height, volBuff, tex->width, tex->height, normalMap );
			offset = tex->width * tex->height * 4;
		}
		else
		{
			memcpy( volBuff, pic->buffer, offset );
		}

		if( tex->format == GU_PSM_8888 )
		{
			if ( IsLightMap( tex ) )
			{
				memcpy( tex->dstTexture, volBuff, texSize );
			}
			else
			{
				GL_TextureSwizzle( tex->dstTexture, volBuff, tex->width * 4, tex->height );
				SetBits( tex->guFlags, GUFLAGTEXSWIZZLED );
			}
		}
		else
		{
			// disable swizzling for lightmaps
			if ( IsLightMap( tex ) )
			{
				GL_PixelConverter( tex->dstTexture, volBuff, tex->width * tex->height, true, tex->format );
			}
			else
			{
/*
				if( !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
						volBuff = GL_ApplyFilter( volBuff, tex->width, tex->height );
*/
				GL_PixelConverter( volBuff + offset, volBuff, tex->width * tex->height, true, tex->format );
				GL_TextureSwizzle( tex->dstTexture, volBuff + offset, tex->width * 2, tex->height );
				SetBits( tex->guFlags, GUFLAGTEXSWIZZLED );
			}
		}
	}
	sceKernelVolatileMemUnlock( 0 );
	sceKernelDcacheWritebackRange( tex->dstTexture, texSize );
	tex->size = texSize;

	SetBits( tex->flags, TF_IMG_UPLOADED ); // done

	return true;
}

/*
===============
GL_UpdateDlightTexture

===============
*/
qboolean GL_UpdateDlightTexture( int texnum, int xoff, int yoff, int width, int height, const void *buffer )
{
	gl_texture_t	*tex;
	size_t			texsize;

	// missed or invalid texture?
	if( texnum <= 0 || texnum >= MAX_TEXTURES )
	{
		if( texnum != 0 )
		{
			gEngfuncs.Con_DPrintf( S_ERROR "GL_UpdateDlightTexture: invalid texture num %d\n", texnum );
			return false;
		}
	}
	tex = &gl_textures[texnum];

	if(tex->width < width || tex->height < height )
	{

		gEngfuncs.Con_DPrintf( S_ERROR "GL_UpdateDlightTexture: invalid update area size %s\n", tex->name );
		return false;
	}

	if(tex->width < xoff || tex->height < yoff )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "GL_UpdateDlightTexture: invalid offset position %s\n", tex->name );
		return false;
	}
	tex->numMips	= 1;
	tex->dstPalette = NULL;
	tex->format		= GU_PSM_5650;

	texsize = tex->width * tex->height * 2;
	if( tex->size != texsize )
	{
		if( tex->dstTexture )
		{
			if( FBitSet( tex->guFlags, GUFLAGTEXINVRAM ) )
				vfree( tex->dstTexture );
			else
				free( tex->dstTexture );

			tex->dstTexture = NULL;
			ClearBits( tex->guFlags, GUFLAGTEXINVRAM );
		}
	}

	if( !tex->dstTexture )
	{
		tex->dstTexture = ( byte* )valloc( texsize );
		if( !tex->dstTexture )
		{
			tex->dstTexture = ( byte* )memalign( 16, texsize );
			if ( !tex->dstTexture )
				gEngfuncs.Host_Error( "GL_UpdateDlightTexture: out of memory! ( texture: %i %s )\n", texsize, tex->name);
		}
		else
		{
			SetBits( tex->guFlags, GUFLAGTEXINVRAM );
		}
		memset( tex->dstTexture, 0x00, texsize );
		SetBits( tex->flags, TF_IMG_UPLOADED ); // done
	}

	tex->size = texsize;
	byte *src = ( byte* )buffer;
	byte *dst = tex->dstTexture;

	dst += ( yoff * tex->width + xoff ) * 2;
	while( height-- )
	{
		GL_PixelConverter( dst, src, width, true, tex->format );
		dst += tex->width * 2;
		src += width * 4;
	}
	sceKernelDcacheWritebackRange( tex->dstTexture, texsize );
	return true;
}

/*
===============
GL_ProcessImage

do specified actions on pixels
===============
*/
static void GL_ProcessImage( gl_texture_t *tex, rgbdata_t *pic )
{
	float	emboss_scale = 0.0f;
	uint	img_flags = 0;

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	tex->encode = pic->encode; // share encode method

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( tex->flags & TF_ALLOW_EMBOSS )
		{
			img_flags |= IMAGE_EMBOSS;
			//tex->flags &= ~TF_ALLOW_EMBOSS;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = gEngfuncs.FS_CopyImage( pic ); // because current pic will be expanded to rgba
#if 0 // forced indexed textures
		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;
#endif
		// dedicated server doesn't register this variable
		if( gl_emboss_scale != NULL )
			emboss_scale = gl_emboss_scale->value;

		// processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer ) gEngfuncs.Image_Process( &pic, 0, 0, img_flags, emboss_scale );

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

/*
================
GL_CheckTexName
================
*/
qboolean GL_CheckTexName( const char *name )
{
	if( !COM_CheckString( name ))
		return false;

	// because multi-layered textures can exceed name string
	if( Q_strlen( name ) >= sizeof( gl_textures->name ))
	{
		gEngfuncs.Con_Printf( S_ERROR "LoadTexture: too long name %s (%d)\n", name, Q_strlen( name ));
		return false;
	}

	return true;
}

/*
================
GL_TextureForName
================
*/
static gl_texture_t *GL_TextureForName( const char *name )
{
	gl_texture_t	*tex;
	uint		hash;

	// find the texture in array
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = gl_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return tex;
	}

	return NULL;
}

/*
================
GL_AllocTexture
================
*/
static gl_texture_t *GL_AllocTexture( const char *name, texFlags_t flags )
{
	gl_texture_t	*tex;
	uint		i;

	// find a free texture_t slot
	for( i = 0, tex = gl_textures; i < gl_numTextures; i++, tex++ )
		if( !tex->name[0] ) break;

	if( i == gl_numTextures )
	{
		if( gl_numTextures == MAX_TEXTURES )
			gEngfuncs.Host_Error( "GL_AllocTexture: MAX_TEXTURES limit exceeds\n" );
		gl_numTextures++;
	}

	tex = &gl_textures[i];

	// copy initial params
	Q_strncpy( tex->name, name, sizeof( tex->name ));
#if 0
	if( FBitSet( flags, TF_SKYSIDE ))
		tex->texnum = tr.skyboxbasenum++;
	else tex->texnum = i; // texnum is used for fast acess into gl_textures array too
#endif
	tex->flags = flags;

	// add to hash table
	tex->hashValue = COM_HashKey( name, TEXTURES_HASH_SIZE );
	tex->nextHash = gl_texturesHashTable[tex->hashValue];
	gl_texturesHashTable[tex->hashValue] = tex;

	return tex;
}

/*
================
GL_DeleteTexture
================
*/
static void GL_DeleteTexture( gl_texture_t *tex )
{
	gl_texture_t	**prev;
	gl_texture_t	*cur;

	ASSERT( tex != NULL );

	// already freed?
	if( !tex->dstTexture && !tex->name[0] ) return;

	// debug
	if( !tex->name[0] )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_DeleteTexture: trying to free unnamed texture\n");
		return;
	}

	// remove from hash table
	prev = &gl_texturesHashTable[tex->hashValue];
	while( 1 )
	{
		cur = *prev;
		if( !cur ) break;

		if( cur == tex )
		{
			*prev = cur->nextHash;
			break;
		}
		prev = &cur->nextHash;
	}

	// release source
	if( tex->original )
		gEngfuncs.FS_FreeImage( tex->original );

	if( tex->dstPalette )
		free( tex->dstPalette );

	if( tex->dstTexture )
	{
		if( FBitSet( tex->guFlags, GUFLAGTEXINVRAM ) )
			vfree( tex->dstTexture );
		else
			free( tex->dstTexture );
	}
	memset( tex, 0, sizeof( *tex ));
}

/*
================
GL_UpdateTexSize

recalc image room
================
*/
void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
#if 0
	int		i, j, texsize;
	int		numSides;
	gl_texture_t	*tex;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return;

	tex = &gl_textures[texnum];
	numSides = FBitSet( tex->flags, TF_CUBEMAP ) ? 6 : 1;
	GL_SetTextureDimensions( tex, width, height, depth );
	tex->size = 0; // recompute now

	for( i = 0; i < numSides; i++ )
	{
		for( j = 0; j < Q_max( 1, tex->numMips ); j++ )
		{
			width = Q_max( 1, ( tex->width >> j ));
			height = Q_max( 1, ( tex->height >> j ));
			texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
			tex->size += texsize;
		}
	}
#endif
}

/*
================
GL_LoadTexture
================
*/
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	gl_texture_t	*tex;
	rgbdata_t		*pic;
	uint		picFlags = 0;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )))
		return (tex - gl_textures);

	if( FBitSet( flags, TF_NOFLIP_TGA ))
		SetBits( picFlags, IL_DONTFLIP_TGA );
#if 1
	SetBits( picFlags, IL_KEEP_8BIT );
#else
	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		SetBits( picFlags, IL_KEEP_8BIT );
#endif
	// set some image flags
	gEngfuncs.Image_SetForceFlags( picFlags );

	pic = gEngfuncs.FS_LoadImage( name, buf, size );
	if( !pic ) return 0; // couldn't loading image

	// allocate the new one
	tex = GL_AllocTexture( name, flags );
	GL_ProcessImage( tex, pic );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gl_texture_t ));
		gEngfuncs.FS_FreeImage( pic ); // release source texture
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	gEngfuncs.FS_FreeImage( pic ); // release source texture

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return tex - gl_textures;
}

/*
================
GL_LoadTextureArray
================
*/
int GL_LoadTextureArray( const char **names, int flags )
{
#if 1
	return 0;
#else
	rgbdata_t		*pic, *src;
	char		basename[256];
	uint		numLayers = 0;
	uint		picFlags = 0;
	char		name[256];
	gl_texture_t	*tex;
	uint		i, j;

	if( !names || !names[0] || !glw_state.initialized )
		return 0;

	// count layers (g-cont. this is pontentially unsafe loop)
	for( i = 0; i < glConfig.max_2d_texture_layers && ( *names[i] != '\0' ); i++ )
		numLayers++;
	name[0] = '\0';

	if( numLayers <= 0 ) return 0;

	// create complexname from layer names
	for( i = 0; i < numLayers; i++ )
	{
		COM_FileBase( names[i], basename );
		Q_strncat( name, va( "%s", basename ), sizeof( name ));
		if( i != ( numLayers - 1 )) Q_strncat( name, "|", sizeof( name ));
	}

	Q_strncat( name, va( "[%i]", numLayers ), sizeof( name ));

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )))
		return (tex - gl_textures);

	// load all the images and pack it into single image
	for( i = 0, pic = NULL; i < numLayers; i++ )
	{
		size_t	srcsize, dstsize, mipsize;

		src = gEngfuncs.FS_LoadImage( names[i], NULL, 0 );
		if( !src ) break; // coldn't find layer

		if( pic )
		{
			// mixed mode: DXT + RGB
			if( pic->type != src->type )
			{
				gEngfuncs.Con_Printf( S_ERROR "GL_LoadTextureArray: mismatch image format for %s and %s\n", names[0], names[i] );
				break;
			}

			// different mipcount
			if( pic->numMips != src->numMips )
			{
				gEngfuncs.Con_Printf( S_ERROR "GL_LoadTextureArray: mismatch mip count for %s and %s\n", names[0], names[i] );
				break;
			}

			if( pic->encode != src->encode )
			{
				gEngfuncs.Con_Printf( S_ERROR "GL_LoadTextureArray: mismatch custom encoding for %s and %s\n", names[0], names[i] );
				break;
			}

			// but allow to rescale raw images
			if( ImageRAW( pic->type ) && ImageRAW( src->type ) && ( pic->width != src->width || pic->height != src->height ))
				gEngfuncs.Image_Process( &src, pic->width, pic->height, IMAGE_RESAMPLE, 0.0f );

			if( pic->size != src->size )
			{
				gEngfuncs.Con_Printf( S_ERROR "GL_LoadTextureArray: mismatch image size for %s and %s\n", names[0], names[i] );
				break;
			}
		}
		else
		{
			// create new image
			pic = Mem_Malloc( gEngfuncs.Image_GetPool(), sizeof( rgbdata_t ));
			memcpy( pic, src, sizeof( rgbdata_t ));

			// expand pic buffer for all layers
			pic->buffer = Mem_Malloc( gEngfuncs.Image_GetPool(), pic->size * numLayers );
			pic->depth = 0;
		}

		mipsize = srcsize = dstsize = 0;

		for( j = 0; j < max( 1, pic->numMips ); j++ )
		{
			int width = Q_max( 1, ( pic->width >> j ));
			int height = Q_max( 1, ( pic->height >> j ));
			mipsize = GL_CalcImageSize( pic->type, width, height, 1 );
			memcpy( pic->buffer + dstsize + mipsize * i, src->buffer + srcsize, mipsize );
			dstsize += mipsize * numLayers;
			srcsize += mipsize;
		}

		gEngfuncs.FS_FreeImage( src );

		// increase layers
		pic->depth++;
	}

	// there were errors
	if( !pic || ( pic->depth != numLayers ))
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_LoadTextureArray: not all layers were loaded. Texture array is not created\n" );
		if( pic ) gEngfuncs.FS_FreeImage( pic );
		return 0;
	}

	// it's multilayer image!
	SetBits( pic->flags, IMAGE_MULTILAYER );
	pic->size *= numLayers;

	// allocate the new one
	tex = GL_AllocTexture( name, flags );
	GL_ProcessImage( tex, pic );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gl_texture_t ));
		gEngfuncs.FS_FreeImage( pic ); // release source texture
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	gEngfuncs.FS_FreeImage( pic ); // release source texture

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return tex - gl_textures;
#endif
}

/*
================
GL_LoadTextureFromBuffer
================
*/
int GL_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	gl_texture_t	*tex;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )) && !update )
		return (tex - gl_textures);

	// couldn't loading image
	if( !pic ) return 0;

	if( update )
	{
		if( tex == NULL )
			gEngfuncs.Host_Error( "GL_LoadTextureFromBuffer: couldn't find texture %s for update\n", name );
		SetBits( tex->flags, flags );
	}
	else
	{
		// allocate the new one
		tex = GL_AllocTexture( name, flags );
	}

	GL_ProcessImage( tex, pic );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gl_texture_t ));
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	return (tex - gl_textures);
}

/*
================
GL_CreateTexture

creates texture from buffer
================
*/
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	qboolean	update = FBitSet( flags, TF_UPDATE ) ? true : false;
	int	datasize = 1;
	rgbdata_t	r_empty;

	if( FBitSet( flags, TF_ARB_16BIT ))
		datasize = 2;
	else if( FBitSet( flags, TF_ARB_FLOAT ))
		datasize = 4;

	ClearBits( flags, TF_UPDATE );
	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * datasize * 4;
	r_empty.buffer = (byte *)buffer;

	// clear invalid combinations
	ClearBits( flags, TF_TEXTURE_3D );

	// if image not luminance and not alphacontrast it will have color
	if( !FBitSet( flags, TF_LUMINANCE ) && !FBitSet( flags, TF_ALPHACONTRAST ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );
#if 0
	if( FBitSet( flags, TF_CUBEMAP ))
	{
		if( !GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
			return 0;
		SetBits( r_empty.flags, IMAGE_CUBEMAP );
		r_empty.size *= 6;
	}
#endif
	return GL_LoadTextureFromBuffer( name, &r_empty, flags, update );
}

/*
================
GL_CreateTextureArray

creates texture array from buffer
================
*/
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
#if 1
	return 0;
#else
	rgbdata_t	r_empty;

	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = Q_max( width, 1 );
	r_empty.height = Q_max( height, 1 );
	r_empty.depth = Q_max( depth, 1 );
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * r_empty.depth * 4;
	r_empty.buffer = (byte *)buffer;

	// clear invalid combinations
	ClearBits( flags, TF_CUBEMAP|TF_SKYSIDE|TF_HAS_LUMA|TF_MAKELUMA|TF_ALPHACONTRAST );

	// if image not luminance it will have color
	if( !FBitSet( flags, TF_LUMINANCE ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );

	if( FBitSet( flags, TF_TEXTURE_3D ))
	{
		if( !GL_Support( GL_TEXTURE_3D_EXT ))
			return 0;
	}
	else
	{
		if( !GL_Support( GL_TEXTURE_ARRAY_EXT ))
			return 0;
		SetBits( r_empty.flags, IMAGE_MULTILAYER );
	}

	return GL_LoadTextureInternal( name, &r_empty, flags );
#endif
}

/*
================
GL_FindTexture
================
*/
int GL_FindTexture( const char *name )
{
	gl_texture_t	*tex;

	if( !GL_CheckTexName( name ))
		return 0;

	// see if already loaded
	if(( tex = GL_TextureForName( name )))
		return (tex - gl_textures);

	return 0;
}

/*
================
GL_FreeTexture
================
*/
void GL_FreeTexture( GLenum texnum )
{
	// number 0 it's already freed
	if( texnum <= 0 ) return;

	GL_DeleteTexture( &gl_textures[texnum] );
}

/*
================
GL_ProcessTexture
================
*/
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	gl_texture_t	*image;
	rgbdata_t		*pic;
	int		flags = 0;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return; // missed image
	image = &gl_textures[texnum];

	// select mode
	if( gamma != -1.0f )
	{
		flags = IMAGE_LIGHTGAMMA;
	}
	else if( topColor != -1 && bottomColor != -1 )
	{
		flags = IMAGE_REMAP;
	}
	else
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: bad operation for %s\n", image->name );
		return;
	}

	if( !image->original )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: no input data for %s\n", image->name );
		return;
	}

	if( ImageDXT( image->original->type ))
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: can't process compressed texture %s\n", image->name );
		return;
	}

	// all the operations makes over the image copy not an original
	pic = gEngfuncs.FS_CopyImage( image->original );
	gEngfuncs.Image_Process( &pic, topColor, bottomColor, flags, 0.0f );

	GL_UploadTexture( image, pic );
	GL_ApplyTextureParams( image ); // update texture filter, wrap etc

	gEngfuncs.FS_FreeImage( pic );
}

/*
================
GL_TexMemory

return size of all uploaded textures
================
*/
int GL_TexMemory( void )
{
	int	i, total = 0;

	for( i = 0; i < gl_numTextures; i++ )
		total += gl_textures[i].size;

	return total;
}

/*
==============================================================================

INTERNAL TEXTURES

==============================================================================
*/
/*
==================
GL_FakeImage
==================
*/
static rgbdata_t *GL_FakeImage( int width, int height, int depth, int flags )
{
	static byte	data2D[1024]; // 16x16x4
	static rgbdata_t	r_image;

	// also use this for bad textures, but without alpha
	r_image.width = Q_max( 1, width );
	r_image.height = Q_max( 1, height );
	r_image.depth = Q_max( 1, depth );
	r_image.flags = flags;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * r_image.depth * 4;
	r_image.buffer = (r_image.size > sizeof( data2D )) ? NULL : data2D;
	r_image.palette = NULL;
	r_image.numMips = 1;
	r_image.encode = 0;

	if( FBitSet( r_image.flags, IMAGE_CUBEMAP ))
		r_image.size *= 6;
	memset( data2D, 0xFF, sizeof( data2D ));

	return &r_image;
}

/*
==================
R_InitDlightTexture
==================
*/
void R_InitDlightTexture( void )
{
	rgbdata_t	r_image;

	if( tr.dlightTexture != 0 )
		return; // already initialized

	memset( &r_image, 0, sizeof( r_image ));
	r_image.width = BLOCK_SIZE;
	r_image.height = BLOCK_SIZE;
	r_image.flags = IMAGE_HAS_COLOR;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;

	tr.dlightTexture = GL_LoadTextureInternal( "*dlight", &r_image, TF_NOMIPMAP|TF_CLAMP|TF_ATLAS_PAGE );
}

/*
==================
GL_CreateInternalTextures
==================
*/
static void GL_CreateInternalTextures( void )
{
	int	dx2, dy, d;
	int	x, y;
	rgbdata_t	*pic;

	// emo-texture from quake1
	pic = GL_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR );

	for( y = 0; y < 16; y++ )
	{
		for( x = 0; x < 16; x++ )
		{
			if(( y < 8 ) ^ ( x < 8 ))
				((uint *)pic->buffer)[y*16+x] = 0xFFFF00FF;
			else ((uint *)pic->buffer)[y*16+x] = 0xFF000000;
		}
	}

	tr.defaultTexture = GL_LoadTextureInternal( REF_DEFAULT_TEXTURE, pic, TF_COLORMAP );

	// particle texture from quake1
	pic = GL_FakeImage( 16, 16, 1, IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA );

	for( x = 0; x < 16; x++ )
	{
		dx2 = x - 8;
		dx2 = dx2 * dx2;

		for( y = 0; y < 16; y++ )
		{
			dy = y - 8;
			d = 255 - 35 * sqrt( dx2 + dy * dy );
			pic->buffer[( y * 16 + x ) * 4 + 3] = bound( 0, d, 255 );
		}
	}

	tr.particleTexture = GL_LoadTextureInternal( REF_PARTICLE_TEXTURE, pic, TF_CLAMP );

	// white texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFFFFFFFF;
	tr.whiteTexture = GL_LoadTextureInternal( REF_WHITE_TEXTURE, pic, TF_COLORMAP );

	// gray texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF7F7F7F;
	tr.grayTexture = GL_LoadTextureInternal( REF_GRAY_TEXTURE, pic, TF_COLORMAP );

	// black texture
	pic = GL_FakeImage( 4, 4, 1, IMAGE_HAS_COLOR );
	for( x = 0; x < 16; x++ )
		((uint *)pic->buffer)[x] = 0xFF000000;
	tr.blackTexture = GL_LoadTextureInternal( REF_BLACK_TEXTURE, pic, TF_COLORMAP );

	// cinematic dummy
	pic = GL_FakeImage( 640, 100, 1, IMAGE_HAS_COLOR );
	tr.cinTexture = GL_LoadTextureInternal( "*cintexture", pic, TF_NOMIPMAP|TF_CLAMP );
}

/*
===============
R_TextureList_f
===============
*/
void R_TextureList_f( void )
{
	gl_texture_t	*image;
	int		i, texCount, ram_bytes = 0, vram_bytes = 0;

	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( " -id-   -w-  -h-    -size-     -fmt-  -type-   -encode-   -wrap-   -depth- -name--------\n" );

	for( i = texCount = 0, image = gl_textures; i < gl_numTextures; i++, image++ )
	{
		if( !image->dstTexture ) continue;

		if( image->dstPalette ) ram_bytes += PALETTE_SIZE;

		vram_bytes += ( FBitSet( image->guFlags, GUFLAGTEXINVRAM ) ) ? image->size : 0;
		ram_bytes += ( FBitSet( image->guFlags, GUFLAGTEXINVRAM ) ) ? 0 : image->size;
		texCount++;

		gEngfuncs.Con_Printf( "%4i: ", i );
		gEngfuncs.Con_Printf( "%4i %4i ", image->width, image->height );
		gEngfuncs.Con_Printf( "%12s ", Q_memprint( image->size ));

		switch( image->format )
		{
		case GU_PSM_T4:
			gEngfuncs.Con_Printf( "T4    " );
			break;
		case GU_PSM_T8:
			gEngfuncs.Con_Printf( "T8    " );
			break;
		case GU_PSM_T16:
			gEngfuncs.Con_Printf( "T16   " );
			break;
		case GU_PSM_T32:
			gEngfuncs.Con_Printf( "T32   " );
			break;
		case GU_PSM_DXT1:
			gEngfuncs.Con_Printf( "DXT1  " );
			break;
		case GU_PSM_DXT3:
			gEngfuncs.Con_Printf( "DXT3  " );
			break;
		case GU_PSM_DXT5:
			gEngfuncs.Con_Printf( "DXT5  " );
			break;
		case GU_PSM_4444:
			gEngfuncs.Con_Printf( "4444  " );
			break;
		case GU_PSM_5551:
			gEngfuncs.Con_Printf( "5551  " );
			break;
		case GU_PSM_5650:
			gEngfuncs.Con_Printf( "5650  " );
			break;
		case GU_PSM_8888:
			gEngfuncs.Con_Printf( "8888  " );
			break;
		default:
			gEngfuncs.Con_Printf( " ^1ERROR^7 " );
			break;
		}

		if( image->flags & TF_NORMALMAP )
			gEngfuncs.Con_Printf( "normal  " );
		else gEngfuncs.Con_Printf( "diffuse " );

		switch( image->encode )
		{
		case DXT_ENCODE_COLOR_YCoCg:
			gEngfuncs.Con_Printf( "YCoCg     " );
			break;
		case DXT_ENCODE_NORMAL_AG_ORTHO:
			gEngfuncs.Con_Printf( "ortho     " );
			break;
		case DXT_ENCODE_NORMAL_AG_STEREO:
			gEngfuncs.Con_Printf( "stereo    " );
			break;
		case DXT_ENCODE_NORMAL_AG_PARABOLOID:
			gEngfuncs.Con_Printf( "parabolic " );
			break;
		case DXT_ENCODE_NORMAL_AG_QUARTIC:
			gEngfuncs.Con_Printf( "quartic   " );
			break;
		case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
			gEngfuncs.Con_Printf( "azimuthal " );
			break;
		default:
			gEngfuncs.Con_Printf( "default   " );
			break;
		}

		if( image->flags & TF_CLAMP )
			gEngfuncs.Con_Printf( "clamp  " );
		else if( image->flags & TF_BORDER )
			gEngfuncs.Con_Printf( "border " );
		else gEngfuncs.Con_Printf( "repeat " );
		gEngfuncs.Con_Printf( "   %d  ", image->depth );
		gEngfuncs.Con_Printf( "  %s\n", image->name );
	}

	gEngfuncs.Con_Printf( "---------------------------------------------------------\n" );
	gEngfuncs.Con_Printf( "%i total textures\n", texCount );
	gEngfuncs.Con_Printf( "%i max index\n", gl_numTextures );
	gEngfuncs.Con_Printf( "%s total ram memory used\n", Q_memprint( ram_bytes ));
	gEngfuncs.Con_Printf( "%s total vram memory used\n", Q_memprint( vram_bytes ));
	gEngfuncs.Con_Printf( "\n" );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void )
{
	memset( gl_textures, 0, sizeof( gl_textures ));
	memset( gl_texturesHashTable, 0, sizeof( gl_texturesHashTable ));
	gl_numTextures = 0;

	// create unused 0-entry
	Q_strncpy( gl_textures->name, "*unused*", sizeof( gl_textures->name ));
	gl_textures->hashValue = COM_HashKey( gl_textures->name, TEXTURES_HASH_SIZE );
	gl_textures->nextHash = gl_texturesHashTable[gl_textures->hashValue];
	gl_texturesHashTable[gl_textures->hashValue] = gl_textures;
	gl_numTextures = 1;

	// validate cvars
	R_SetTextureParameters();
	GL_CreateInternalTextures();

	gEngfuncs.Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void )
{
	gl_texture_t	*tex;
	int		i;

	gEngfuncs.Cmd_RemoveCommand( "texturelist" );
	GL_CleanupAllTextureUnits();

	for( i = 0, tex = gl_textures; i < gl_numTextures; i++, tex++ )
		GL_DeleteTexture( tex );

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	memset( gl_texturesHashTable, 0, sizeof( gl_texturesHashTable ));
	memset( gl_textures, 0, sizeof( gl_textures ));
	gl_numTextures = 0;
}
