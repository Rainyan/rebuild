//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Functionality to render a glowing outline around client renderable objects.
//
//===============================================================================

#include "cbase.h"
#include "glow_outline_effect.h"
#include "model_types.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "view_shared.h"
#include "viewpostprocess.h"
#if defined GLOWS_ENABLE && defined NEO
#include "neo_gamerules.h"
#endif // GLOWS_ENABLE && NEO

#define FULL_FRAME_TEXTURE "_rt_FullFrameFB"

#ifdef GLOWS_ENABLE

#ifdef NEO
ConVar glow_outline_effect_center_alpha("glow_outline_effect_center_alpha", "0.05f", FCVAR_ARCHIVE, "Opacity of the part of the glow effect drawn on top of the player model", true, 0.f, true, 1.f);

extern ConVar mp_forcecamera;
static void glowOutlineEffectToggleCallBack(IConVar* var, const char* pOldValue, float flOldValue)
{
	if (!flOldValue && GetLocalPlayerTeam() != TEAM_SPECTATOR && mp_forcecamera.GetInt() != OBS_ALLOW_ALL)
	{
		var->SetValue(false);
		return;
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (auto player = UTIL_PlayerByIndex(i))
		{
			float r, g, b;
			NEORules()->GetTeamGlowColor(player->GetTeamNumber(), r, g, b);
			player->SetGlowEffectColor(r, g, b);
			player->SetClientSideGlowEnabled(!flOldValue);
		}
	}
}
#endif // NEO
ConVar glow_outline_effect_enable( "glow_outline_effect_enable", "0", 0, "Enable entity outline glow effects.", glowOutlineEffectToggleCallBack);
#ifdef NEO
ConVar glow_outline_effect_width( "glow_outline_effect_width", "0.5f", FCVAR_ARCHIVE, "Width of glow outline effect in screen space.", true, 0.f, false, 0.f);
#else
ConVar glow_outline_effect_width( "glow_outline_width", "10.0f", FCVAR_CHEAT, "Width of glow outline effect in screen space." );
#endif // NEO

extern bool g_bDumpRenderTargets; // in viewpostprocess.cpp

CGlowObjectManager g_GlowObjectManager;

struct ShaderStencilState_t
{
	bool m_bEnable;
	StencilOperation_t m_FailOp;
	StencilOperation_t m_ZFailOp;
	StencilOperation_t m_PassOp;
	StencilComparisonFunction_t m_CompareFunc;
	int m_nReferenceValue;
	uint32 m_nTestMask;
	uint32 m_nWriteMask;

	ShaderStencilState_t()
	{
		m_bEnable = false;
		m_PassOp = m_FailOp = m_ZFailOp = STENCILOPERATION_KEEP;
		m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
		m_nReferenceValue = 0;
		m_nTestMask = m_nWriteMask = 0xFFFFFFFF;
	}

	void SetStencilState( CMatRenderContextPtr &pRenderContext  )
	{
		pRenderContext->SetStencilEnable( m_bEnable );
		pRenderContext->SetStencilFailOperation( m_FailOp );
		pRenderContext->SetStencilZFailOperation( m_ZFailOp );
		pRenderContext->SetStencilPassOperation( m_PassOp );
		pRenderContext->SetStencilCompareFunction( m_CompareFunc );
		pRenderContext->SetStencilReferenceValue( m_nReferenceValue );
		pRenderContext->SetStencilTestMask( m_nTestMask );
		pRenderContext->SetStencilWriteMask( m_nWriteMask );
	}
};

void CGlowObjectManager::RenderGlowEffects( const CViewSetup *pSetup, int nSplitScreenSlot )
{
	if ( g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_0() )
	{
		if ( glow_outline_effect_enable.GetBool() )
		{
			CMatRenderContextPtr pRenderContext( materials );

			int nX, nY, nWidth, nHeight;
			pRenderContext->GetViewport( nX, nY, nWidth, nHeight );

			PIXEvent _pixEvent( pRenderContext, "EntityGlowEffects" );
#ifdef NEO
			ApplyEntityGlowEffects(pSetup, nSplitScreenSlot, pRenderContext, 0.f, nX, nY, nWidth, nHeight);
#else
			ApplyEntityGlowEffects( pSetup, nSplitScreenSlot, pRenderContext, glow_outline_effect_width.GetFloat(), nX, nY, nWidth, nHeight );
#endif
		}
	}
}

static void SetRenderTargetAndViewPort( ITexture *rt, int w, int h )
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetRenderTarget(rt);
	pRenderContext->Viewport(0,0,w,h);
}

void CGlowObjectManager::RenderGlowModels( const CViewSetup *pSetup, int nSplitScreenSlot, CMatRenderContextPtr &pRenderContext )
{
	//==========================================================================================//
	// This renders solid pixels with the correct coloring for each object that needs the glow.	//
	// After this function returns, this image will then be blurred and added into the frame	//
	// buffer with the objects stenciled out.													//
	//==========================================================================================//
	pRenderContext->PushRenderTargetAndViewport();

	// Save modulation color and blend
	Vector vOrigColor;
	render->GetColorModulation( vOrigColor.Base() );
	float flOrigBlend = render->GetBlend();

	// Get pointer to FullFrameFB
	ITexture *pRtFullFrame = NULL;
	pRtFullFrame = materials->FindTexture( FULL_FRAME_TEXTURE, TEXTURE_GROUP_RENDER_TARGET );

	SetRenderTargetAndViewPort( pRtFullFrame, pSetup->width, pSetup->height );

	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false, false );

	// Set override material for glow color
	IMaterial *pMatGlowColor = NULL;

	pMatGlowColor = materials->FindMaterial( "dev/glow_color", TEXTURE_GROUP_OTHER, true );
#ifndef NEO
	g_pStudioRender->ForcedMaterialOverride( pMatGlowColor );
#endif

	ShaderStencilState_t stencilState;
	stencilState.m_bEnable = false;
	stencilState.m_nReferenceValue = 0;
	stencilState.m_nTestMask = 0xFF;
	stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
	stencilState.m_PassOp = STENCILOPERATION_KEEP;
	stencilState.m_FailOp = STENCILOPERATION_KEEP;
	stencilState.m_ZFailOp = STENCILOPERATION_KEEP;

	stencilState.SetStencilState( pRenderContext );

	//==================//
	// Draw the objects //
	//==================//
	for ( int i = 0; i < m_GlowObjectDefinitions.Count(); ++ i )
	{
		if ( m_GlowObjectDefinitions[i].IsUnused() || !m_GlowObjectDefinitions[i].ShouldDraw( nSplitScreenSlot ) )
			continue;

#ifdef NEO
		// DrawModel can call ForcedMaterialOverride also
		g_pStudioRender->ForcedMaterialOverride(pMatGlowColor);
#endif
		render->SetBlend( m_GlowObjectDefinitions[i].m_flGlowAlpha );
		Vector vGlowColor = m_GlowObjectDefinitions[i].m_vGlowColor * m_GlowObjectDefinitions[i].m_flGlowAlpha;
		render->SetColorModulation( &vGlowColor[0] ); // This only sets rgb, not alpha

		m_GlowObjectDefinitions[i].DrawModel();
	}

	if ( g_bDumpRenderTargets )
	{
		DumpTGAofRenderTarget( pSetup->width, pSetup->height, "GlowModels" );
	}

	g_pStudioRender->ForcedMaterialOverride( NULL );
	render->SetColorModulation( vOrigColor.Base() );
	render->SetBlend( flOrigBlend );
	
	ShaderStencilState_t stencilStateDisable;
	stencilStateDisable.m_bEnable = false;
	stencilStateDisable.SetStencilState( pRenderContext );

	pRenderContext->PopRenderTargetAndViewport();
}

void CGlowObjectManager::ApplyEntityGlowEffects( const CViewSetup *pSetup, int nSplitScreenSlot, CMatRenderContextPtr &pRenderContext, float flBloomScale, int x, int y, int w, int h )
{
	//=======================================================//
	// Render objects into stencil buffer					 //
	//=======================================================//
	// Set override shader to the same simple shader we use to render the glow models
	IMaterial *pMatGlowColor = materials->FindMaterial( "dev/glow_color", TEXTURE_GROUP_OTHER, true );
	g_pStudioRender->ForcedMaterialOverride( pMatGlowColor );

	ShaderStencilState_t stencilStateDisable;
	stencilStateDisable.m_bEnable = false;
	float flSavedBlend = render->GetBlend();

	// Set alpha to 0 so we don't touch any color pixels
	render->SetBlend( 0.0f );
	pRenderContext->OverrideDepthEnable( true, false );

	int iNumGlowObjects = 0;

	for ( int i = 0; i < m_GlowObjectDefinitions.Count(); ++ i )
	{
		if ( m_GlowObjectDefinitions[i].IsUnused() || !m_GlowObjectDefinitions[i].ShouldDraw( nSplitScreenSlot ) )
			continue;

		if ( m_GlowObjectDefinitions[i].m_bRenderWhenOccluded || m_GlowObjectDefinitions[i].m_bRenderWhenUnoccluded )
		{
			if ( m_GlowObjectDefinitions[i].m_bRenderWhenOccluded && m_GlowObjectDefinitions[i].m_bRenderWhenUnoccluded )
			{
				ShaderStencilState_t stencilState;
				stencilState.m_bEnable = true;
				stencilState.m_nReferenceValue = 1;
				stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
				stencilState.m_PassOp = STENCILOPERATION_REPLACE;
				stencilState.m_FailOp = STENCILOPERATION_KEEP;
				stencilState.m_ZFailOp = STENCILOPERATION_REPLACE;

				stencilState.SetStencilState( pRenderContext );

				m_GlowObjectDefinitions[i].DrawModel();
			}
			else if ( m_GlowObjectDefinitions[i].m_bRenderWhenOccluded )
			{
				ShaderStencilState_t stencilState;
				stencilState.m_bEnable = true;
				stencilState.m_nReferenceValue = 1;
				stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
				stencilState.m_PassOp = STENCILOPERATION_KEEP;
				stencilState.m_FailOp = STENCILOPERATION_KEEP;
				stencilState.m_ZFailOp = STENCILOPERATION_REPLACE;

				stencilState.SetStencilState( pRenderContext );

				m_GlowObjectDefinitions[i].DrawModel();
			}
			else if ( m_GlowObjectDefinitions[i].m_bRenderWhenUnoccluded )
			{
				ShaderStencilState_t stencilState;
				stencilState.m_bEnable = true;
				stencilState.m_nReferenceValue = 2;
				stencilState.m_nTestMask = 0x1;
				stencilState.m_nWriteMask = 0x3;
				stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
				stencilState.m_PassOp = STENCILOPERATION_INCRSAT;
				stencilState.m_FailOp = STENCILOPERATION_KEEP;
				stencilState.m_ZFailOp = STENCILOPERATION_REPLACE;

				stencilState.SetStencilState( pRenderContext );

				m_GlowObjectDefinitions[i].DrawModel();
			}
		}

		iNumGlowObjects++;
	}

	// Need to do a 2nd pass to warm stencil for objects which are rendered only when occluded
	for ( int i = 0; i < m_GlowObjectDefinitions.Count(); ++ i )
	{
		if ( m_GlowObjectDefinitions[i].IsUnused() || !m_GlowObjectDefinitions[i].ShouldDraw( nSplitScreenSlot ) )
			continue;

		if ( m_GlowObjectDefinitions[i].m_bRenderWhenOccluded && !m_GlowObjectDefinitions[i].m_bRenderWhenUnoccluded )
		{
			ShaderStencilState_t stencilState;
			stencilState.m_bEnable = true;
			stencilState.m_nReferenceValue = 2;
			stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_ALWAYS;
			stencilState.m_PassOp = STENCILOPERATION_REPLACE;
			stencilState.m_FailOp = STENCILOPERATION_KEEP;
			stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
			stencilState.SetStencilState( pRenderContext );

			m_GlowObjectDefinitions[i].DrawModel();
		}
	}

	pRenderContext->OverrideDepthEnable( false, false );
	render->SetBlend( flSavedBlend );
	stencilStateDisable.SetStencilState( pRenderContext );
	g_pStudioRender->ForcedMaterialOverride( NULL );

	// If there aren't any objects to glow, don't do all this other stuff
	// this fixes a bug where if there are glow objects in the list, but none of them are glowing,
	// the whole screen blooms.
	if ( iNumGlowObjects <= 0 )
		return;

	//=============================================
	// Render the glow colors to _rt_FullFrameFB 
	//=============================================
	{
		PIXEvent pixEvent( pRenderContext, "RenderGlowModels" );
		RenderGlowModels( pSetup, nSplitScreenSlot, pRenderContext );
	}
	
	// Get viewport
	int nSrcWidth = pSetup->width;
	int nSrcHeight = pSetup->height;
	int nViewportX, nViewportY, nViewportWidth, nViewportHeight;
	pRenderContext->GetViewport( nViewportX, nViewportY, nViewportWidth, nViewportHeight );

	// Get material and texture pointers
#ifdef NEO
	ITexture *pRtQuarterSize1 = materials->FindTexture( FULL_FRAME_TEXTURE, TEXTURE_GROUP_RENDER_TARGET );
#else
	ITexture *pRtQuarterSize1 = materials->FindTexture( "_rt_SmallFB1", TEXTURE_GROUP_RENDER_TARGET );
#endif //NEO

	{
		//=======================================================================================================//
		// At this point, pRtQuarterSize0 is filled with the fully colored glow around everything as solid glowy //
		// blobs. Now we need to stencil out the original objects by only writing pixels that have no            //
		// stencil bits set in the range we care about.                                                          //
		//=======================================================================================================//
		IMaterial *pMatHaloAddToScreen = materials->FindMaterial( "dev/halo_add_to_screen", TEXTURE_GROUP_OTHER, true );
#ifdef NEO
		IMaterial *pMatHaloAddToScreenOutline = materials->FindMaterial( "dev/halo_add_to_screen_outline", TEXTURE_GROUP_OTHER, true );
#endif // NEO

		// Do not fade the glows out at all (weight = 1.0)
		IMaterialVar *pDimVar = pMatHaloAddToScreen->FindVar( "$C0_X", NULL );
		pDimVar->SetFloatValue( 1.0f );

		// Set stencil state
		ShaderStencilState_t stencilState;
		stencilState.m_bEnable = true;
		stencilState.m_nWriteMask = 0x0; // We're not changing stencil
		stencilState.m_nTestMask = 0xFF; 
		stencilState.m_nReferenceValue = 0x0;
		stencilState.m_CompareFunc = STENCILCOMPARISONFUNCTION_EQUAL;
		stencilState.m_PassOp = STENCILOPERATION_KEEP;
		stencilState.m_FailOp = STENCILOPERATION_KEEP;
		stencilState.m_ZFailOp = STENCILOPERATION_KEEP;
		stencilState.SetStencilState( pRenderContext );

		// Draw quad
#ifdef NEO
		const float outlineWidth = glow_outline_effect_width.GetFloat();
		if (outlineWidth)
		{
			IMaterialVar* pOutlineVar = pMatHaloAddToScreenOutline->FindVar("$C0_X", NULL);
			pOutlineVar->SetFloatValue(outlineWidth);
			pRenderContext->DrawScreenSpaceRectangle(pMatHaloAddToScreenOutline, 0, 0, nViewportWidth+1, nViewportHeight+1,
				0, 0, pRtQuarterSize1->GetActualWidth(), pRtQuarterSize1->GetActualHeight(),
				pRtQuarterSize1->GetActualWidth(),
				pRtQuarterSize1->GetActualHeight());
		}

		const float alpha = glow_outline_effect_center_alpha.GetFloat();
		if (alpha)
		{
			stencilStateDisable.SetStencilState( pRenderContext );
			pDimVar->SetFloatValue(alpha);
			pRenderContext->DrawScreenSpaceRectangle(pMatHaloAddToScreen, 0, 0, nViewportWidth+1, nViewportHeight+1,
				0, 0, pRtQuarterSize1->GetActualWidth(), pRtQuarterSize1->GetActualHeight(),
				pRtQuarterSize1->GetActualWidth(),
				pRtQuarterSize1->GetActualHeight());
		}
#else
		pRenderContext->DrawScreenSpaceRectangle(pMatHaloAddToScreen, 0, 0, nViewportWidth, nViewportHeight,
			0.0f, -0.5f, nSrcWidth / 4 - 1, nSrcHeight / 4 - 1,
			pRtQuarterSize1->GetActualWidth(),
			pRtQuarterSize1->GetActualHeight() );

		stencilStateDisable.SetStencilState( pRenderContext );
#endif
	}
}

void CGlowObjectManager::GlowObjectDefinition_t::DrawModel()
{
	if ( m_hEntity.Get() )
	{
#ifdef NEO
		m_hEntity->DrawModel( STUDIO_RENDER | STUDIO_IGNORE_NEO_EFFECTS );
#else
		m_hEntity->DrawModel( STUDIO_RENDER );
#endif
		C_BaseEntity *pAttachment = m_hEntity->FirstMoveChild();

		while ( pAttachment != NULL )
		{
			if ( !g_GlowObjectManager.HasGlowEffect( pAttachment ) && pAttachment->ShouldDraw() )
			{
#ifdef NEO
				pAttachment->DrawModel( STUDIO_RENDER | STUDIO_IGNORE_NEO_EFFECTS );
#else
				pAttachment->DrawModel( STUDIO_RENDER );
#endif
			}
			pAttachment = pAttachment->NextMovePeer();
		}
	}
}

#endif // GLOWS_ENABLE
