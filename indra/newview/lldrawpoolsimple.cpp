/** 
 * @file lldrawpoolsimple.cpp
 * @brief LLDrawPoolSimple class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolsimple.h"

#include "llviewercamera.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llviewershadermgr.h"
#include "llrender.h"

#define GE_FORCE_WORKAROUND LL_DARWIN

static LLGLSLShader* simple_shader = NULL;
static LLGLSLShader* fullbright_shader = NULL;

static LLFastTimer::DeclareTimer FTM_RENDER_SIMPLE_DEFERRED("Deferred Simple");
static LLFastTimer::DeclareTimer FTM_RENDER_GRASS_DEFERRED("Deferred Grass");

void LLDrawPoolGlow::beginPostDeferredPass(S32 pass)
{
	gDeferredEmissiveProgram.bind();
	gDeferredEmissiveProgram.uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
}

static LLFastTimer::DeclareTimer FTM_RENDER_GLOW_PUSH("Glow Push");

void LLDrawPoolGlow::renderPostDeferred(S32 pass)
{
	LLFastTimer t(FTM_RENDER_GLOW);
	LLGLEnable blend(GL_BLEND);
	LLGLDisable test(GL_ALPHA_TEST);
	gGL.flush();
	/// Get rid of z-fighting with non-glow pass.
	LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -1.0f);
	gGL.setSceneBlendType(LLRender::BT_ADD);
	
	LLGLDepthTest depth(GL_TRUE, GL_FALSE);
	gGL.setColorMask(false, true);

	{
		LLFastTimer t(FTM_RENDER_GLOW_PUSH);
		pushBatches(LLRenderPass::PASS_GLOW, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
	}
	
	gGL.setColorMask(true, false);
	gGL.setSceneBlendType(LLRender::BT_ALPHA);	
}

void LLDrawPoolGlow::endPostDeferredPass(S32 pass)
{
	gDeferredEmissiveProgram.unbind();
	LLRenderPass::endRenderPass(pass);
}

S32 LLDrawPoolGlow::getNumPasses()
{
	if (LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT) > 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void LLDrawPoolGlow::render(S32 pass)
{
	LLFastTimer t(FTM_RENDER_GLOW);
	LLGLEnable blend(GL_BLEND);
	LLGLDisable test(GL_ALPHA_TEST);
	gGL.flush();
	/// Get rid of z-fighting with non-glow pass.
	LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -1.0f);
	gGL.setSceneBlendType(LLRender::BT_ADD);
	
	U32 shader_level = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);

	//should never get here without basic shaders enabled
	llassert(shader_level > 0);
	
	LLGLSLShader* shader = LLPipeline::sUnderWaterRender ? &gObjectEmissiveWaterProgram : &gObjectEmissiveProgram;
	shader->bind();
	if (LLPipeline::sRenderDeferred)
	{
		shader->uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
	}
	else
	{
		shader->uniform1f(LLShaderMgr::TEXTURE_GAMMA, 1.f);
	}	

	LLGLDepthTest depth(GL_TRUE, GL_FALSE);
	gGL.setColorMask(false, true);

	pushBatches(LLRenderPass::PASS_GLOW, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
	
	gGL.setColorMask(true, false);
	gGL.setSceneBlendType(LLRender::BT_ALPHA);
	
	if (shader_level > 0 && fullbright_shader)
	{
		shader->unbind();
	}
}

void LLDrawPoolGlow::pushBatch(LLDrawInfo& params, U32 mask, BOOL texture, BOOL batch_textures)
{
	//gGL.diffuseColor4ubv(params.mGlowColor.mV);
	LLRenderPass::pushBatch(params, mask, texture, batch_textures);
}


LLDrawPoolSimple::LLDrawPoolSimple() :
	LLRenderPass(POOL_SIMPLE)
{
}

void LLDrawPoolSimple::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

void LLDrawPoolSimple::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_SIMPLE);

	if (LLPipeline::sImpostorRender)
	{
		simple_shader = &gObjectSimpleImpostorProgram;
	}
	else if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectSimpleWaterProgram;
	}
	else
	{
		simple_shader = &gObjectSimpleProgram;
	}

	if (mVertexShaderLevel > 0)
	{
		simple_shader->bind();
	}
	else 
	{
		// don't use shaders!
		if (gGLManager.mHasShaderObjects)
		{
			LLGLSLShader::bindNoShader();
		}		
	}
}

void LLDrawPoolSimple::endRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_SIMPLE);
	stop_glerror();
	LLRenderPass::endRenderPass(pass);
	stop_glerror();
	if (mVertexShaderLevel > 0)
	{
		simple_shader->unbind();
	}
}

void LLDrawPoolSimple::render(S32 pass)
{
	LLGLDisable blend(GL_BLEND);
	
	{ //render simple
		LLFastTimer t(FTM_RENDER_SIMPLE);
		gPipeline.enableLightsDynamic();

		if (mVertexShaderLevel > 0)
		{
			U32 mask = getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX;

			pushBatches(LLRenderPass::PASS_SIMPLE, mask, TRUE, TRUE);

			if (LLPipeline::sRenderDeferred)
			{ //if deferred rendering is enabled, bump faces aren't registered as simple
				//render bump faces here as simple so bump faces will appear under water
				pushBatches(LLRenderPass::PASS_BUMP, mask, TRUE, TRUE);			
				pushBatches(LLRenderPass::PASS_MATERIAL, mask, TRUE, TRUE);
				pushBatches(LLRenderPass::PASS_SPECMAP, mask, TRUE, TRUE);
				pushBatches(LLRenderPass::PASS_NORMMAP, mask, TRUE, TRUE);
				pushBatches(LLRenderPass::PASS_NORMSPEC, mask, TRUE, TRUE);		
			}
		}
		else
		{
			LLGLDisable alpha_test(GL_ALPHA_TEST);
			renderTexture(LLRenderPass::PASS_SIMPLE, getVertexDataMask());
		}
		
	}
}










static LLFastTimer::DeclareTimer FTM_RENDER_ALPHA_MASK("Alpha Mask");

LLDrawPoolAlphaMask::LLDrawPoolAlphaMask() :
	LLRenderPass(POOL_ALPHA_MASK)
{
}

void LLDrawPoolAlphaMask::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

void LLDrawPoolAlphaMask::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);

	if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectSimpleWaterAlphaMaskProgram;
	}
	else
	{
		simple_shader = &gObjectSimpleAlphaMaskProgram;
	}

	if (mVertexShaderLevel > 0)
	{
		simple_shader->bind();
	}
	else 
	{
		// don't use shaders!
		if (gGLManager.mHasShaderObjects)
		{
			LLGLSLShader::bindNoShader();
		}		
	}
}

void LLDrawPoolAlphaMask::endRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);
	stop_glerror();
	LLRenderPass::endRenderPass(pass);
	stop_glerror();
	if (mVertexShaderLevel > 0)
	{
		simple_shader->unbind();
	}
}

void LLDrawPoolAlphaMask::render(S32 pass)
{
	LLGLDisable blend(GL_BLEND);
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);
	
	if (mVertexShaderLevel > 0)
	{
		simple_shader->bind();
		simple_shader->setMinimumAlpha(0.33f);

		pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
		pushMaskBatches(LLRenderPass::PASS_MATERIAL_ALPHA_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
		pushMaskBatches(LLRenderPass::PASS_SPECMAP_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
		pushMaskBatches(LLRenderPass::PASS_NORMMAP_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
		pushMaskBatches(LLRenderPass::PASS_NORMSPEC_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
	}
	else
	{
		LLGLEnable test(GL_ALPHA_TEST);
		pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask(), TRUE, FALSE);
		gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT); //OK
	}
}

LLDrawPoolFullbrightAlphaMask::LLDrawPoolFullbrightAlphaMask() :
	LLRenderPass(POOL_FULLBRIGHT_ALPHA_MASK)
{
}

void LLDrawPoolFullbrightAlphaMask::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

void LLDrawPoolFullbrightAlphaMask::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);

	if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectFullbrightWaterAlphaMaskProgram;
	}
	else
	{
		simple_shader = &gObjectFullbrightAlphaMaskProgram;
	}

	if (mVertexShaderLevel > 0)
	{
		simple_shader->bind();
	}
	else 
	{
		// don't use shaders!
		if (gGLManager.mHasShaderObjects)
		{
			LLGLSLShader::bindNoShader();
		}		
	}
}

void LLDrawPoolFullbrightAlphaMask::endRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);
	stop_glerror();
	LLRenderPass::endRenderPass(pass);
	stop_glerror();
	if (mVertexShaderLevel > 0)
	{
		simple_shader->unbind();
	}
}

void LLDrawPoolFullbrightAlphaMask::render(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK);

	if (mVertexShaderLevel > 0)
	{
		if (simple_shader)
		{
			simple_shader->bind();
			simple_shader->setMinimumAlpha(0.33f);
			if (LLPipeline::sRenderingHUDs || !LLPipeline::sRenderDeferred)
			{
				simple_shader->uniform1f(LLShaderMgr::TEXTURE_GAMMA, 1.0f);
			} else {
				simple_shader->uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
			}
		}
		pushMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
		//LLGLSLShader::bindNoShader();
	}
	else
	{
		LLGLEnable test(GL_ALPHA_TEST);
		gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
		pushMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, getVertexDataMask(), TRUE, FALSE);
		gPipeline.enableLightsDynamic();
		gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT); //OK
	}
}

//===============================
//DEFERRED IMPLEMENTATION
//===============================

void LLDrawPoolSimple::beginDeferredPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_SIMPLE_DEFERRED);
	gDeferredDiffuseProgram.bind();
}

void LLDrawPoolSimple::endDeferredPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_SIMPLE_DEFERRED);
	LLRenderPass::endRenderPass(pass);

	gDeferredDiffuseProgram.unbind();
}

void LLDrawPoolSimple::renderDeferred(S32 pass)
{
	LLGLDisable blend(GL_BLEND);
	LLGLDisable alpha_test(GL_ALPHA_TEST);

	{ //render simple
		LLFastTimer t(FTM_RENDER_SIMPLE_DEFERRED);
		pushBatches(LLRenderPass::PASS_SIMPLE, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
	}
}

static LLFastTimer::DeclareTimer FTM_RENDER_ALPHA_MASK_DEFERRED("Deferred Alpha Mask");

void LLDrawPoolAlphaMask::beginDeferredPass(S32 pass)
{
	
}

void LLDrawPoolAlphaMask::endDeferredPass(S32 pass)
{
	
}

void LLDrawPoolAlphaMask::renderDeferred(S32 pass)
{
	LLFastTimer t(FTM_RENDER_ALPHA_MASK_DEFERRED);
	gDeferredDiffuseAlphaMaskProgram.bind();
	gDeferredDiffuseAlphaMaskProgram.setMinimumAlpha(0.33f);
	pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, getVertexDataMask() | LLVertexBuffer::MAP_TEXTURE_INDEX, TRUE, TRUE);
	gDeferredDiffuseAlphaMaskProgram.unbind();			
}


// grass drawpool
LLDrawPoolGrass::LLDrawPoolGrass() :
 LLRenderPass(POOL_GRASS)
{

}

void LLDrawPoolGrass::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}


void LLDrawPoolGrass::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_GRASS);
	stop_glerror();

	if (LLPipeline::sUnderWaterRender)
	{
		simple_shader = &gObjectAlphaMaskNonIndexedWaterProgram;
	}
	else
	{
		simple_shader = &gObjectAlphaMaskNonIndexedProgram;
	}

	if (mVertexShaderLevel > 0)
	{
		simple_shader->bind();
		simple_shader->setMinimumAlpha(0.5f);
	}
	else 
	{
		gGL.setAlphaRejectSettings(LLRender::CF_GREATER, 0.5f);
		// don't use shaders!
		if (gGLManager.mHasShaderObjects)
		{
			LLGLSLShader::bindNoShader();
		}		
	}
}

void LLDrawPoolGrass::endRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_GRASS);
	LLRenderPass::endRenderPass(pass);

	if (mVertexShaderLevel > 0)
	{
		simple_shader->unbind();
	}
	else
	{
		gGL.setAlphaRejectSettings(LLRender::CF_DEFAULT);
	}
}

void LLDrawPoolGrass::render(S32 pass)
{
	LLGLDisable blend(GL_BLEND);
	
	{
		LLFastTimer t(FTM_RENDER_GRASS);
		LLGLEnable test(GL_ALPHA_TEST);
		gGL.setSceneBlendType(LLRender::BT_ALPHA);
		//render grass
		LLRenderPass::renderTexture(LLRenderPass::PASS_GRASS, getVertexDataMask());
	}
}

void LLDrawPoolGrass::beginDeferredPass(S32 pass)
{

}

void LLDrawPoolGrass::endDeferredPass(S32 pass)
{

}

void LLDrawPoolGrass::renderDeferred(S32 pass)
{
	{
		LLFastTimer t(FTM_RENDER_GRASS_DEFERRED);
		gDeferredNonIndexedDiffuseAlphaMaskProgram.bind();
		gDeferredNonIndexedDiffuseAlphaMaskProgram.setMinimumAlpha(0.5f);
		//render grass
		LLRenderPass::renderTexture(LLRenderPass::PASS_GRASS, getVertexDataMask());
	}			
}


// Fullbright drawpool
LLDrawPoolFullbright::LLDrawPoolFullbright() :
	LLRenderPass(POOL_FULLBRIGHT)
{
}

void LLDrawPoolFullbright::prerender()
{
	mVertexShaderLevel = LLViewerShaderMgr::instance()->getVertexShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

void LLDrawPoolFullbright::beginPostDeferredPass(S32 pass)
{
	if (LLPipeline::sUnderWaterRender)
	{
		gDeferredFullbrightWaterProgram.bind();
	}
	else
	{
		gDeferredFullbrightProgram.bind();
	}
	
}

void LLDrawPoolFullbright::renderPostDeferred(S32 pass)
{
	LLFastTimer t(FTM_RENDER_FULLBRIGHT);
	
	gGL.setSceneBlendType(LLRender::BT_ALPHA);
	U32 fullbright_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXTURE_INDEX;
	pushBatches(LLRenderPass::PASS_FULLBRIGHT, fullbright_mask, TRUE, TRUE);
}

void LLDrawPoolFullbright::endPostDeferredPass(S32 pass)
{
	if (LLPipeline::sUnderWaterRender)
	{
		gDeferredFullbrightWaterProgram.unbind();
	}
	else
	{
		gDeferredFullbrightProgram.unbind();
	}
	LLRenderPass::endRenderPass(pass);
}

void LLDrawPoolFullbright::beginRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_FULLBRIGHT);
	
	if (LLPipeline::sUnderWaterRender)
	{
		fullbright_shader = &gObjectFullbrightWaterProgram;
	}
	else
	{
		fullbright_shader = &gObjectFullbrightProgram;
	}
}

void LLDrawPoolFullbright::endRenderPass(S32 pass)
{
	LLFastTimer t(FTM_RENDER_FULLBRIGHT);
	LLRenderPass::endRenderPass(pass);

	stop_glerror();

	if (mVertexShaderLevel > 0)
	{
		fullbright_shader->unbind();
	}

	stop_glerror();
}

void LLDrawPoolFullbright::render(S32 pass)
{ //render fullbright
	LLFastTimer t(FTM_RENDER_FULLBRIGHT);
	gGL.setSceneBlendType(LLRender::BT_ALPHA);

	stop_glerror();

	if (mVertexShaderLevel > 0)
	{
		fullbright_shader->bind();
		fullbright_shader->uniform1f(LLViewerShaderMgr::FULLBRIGHT, 1.f);
		fullbright_shader->uniform1f(LLViewerShaderMgr::TEXTURE_GAMMA, 1.f);

		U32 fullbright_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXTURE_INDEX;
		pushBatches(LLRenderPass::PASS_FULLBRIGHT, fullbright_mask, TRUE, TRUE);
		pushBatches(LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE, fullbright_mask, TRUE, TRUE);
		pushBatches(LLRenderPass::PASS_SPECMAP_EMISSIVE, fullbright_mask, TRUE, TRUE);
		pushBatches(LLRenderPass::PASS_NORMMAP_EMISSIVE, fullbright_mask, TRUE, TRUE);
		pushBatches(LLRenderPass::PASS_NORMSPEC_EMISSIVE, fullbright_mask, TRUE, TRUE);
	}
	else
	{
		gPipeline.enableLightsFullbright(LLColor4(1,1,1,1));
		U32 fullbright_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_COLOR;
		renderTexture(LLRenderPass::PASS_FULLBRIGHT, fullbright_mask);
		pushBatches(LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE, fullbright_mask);
		pushBatches(LLRenderPass::PASS_SPECMAP_EMISSIVE, fullbright_mask);
		pushBatches(LLRenderPass::PASS_NORMMAP_EMISSIVE, fullbright_mask);
		pushBatches(LLRenderPass::PASS_NORMSPEC_EMISSIVE, fullbright_mask);
	}

	stop_glerror();
}

S32 LLDrawPoolFullbright::getNumPasses()
{ 
	return 1;
}


void LLDrawPoolFullbrightAlphaMask::beginPostDeferredPass(S32 pass)
{
	
	if (LLPipeline::sRenderingHUDs || !LLPipeline::sRenderDeferred)
	{
		gObjectFullbrightAlphaMaskProgram.bind();
		gObjectFullbrightAlphaMaskProgram.uniform1f(LLShaderMgr::TEXTURE_GAMMA, 1.0f);
	} 
	else 
	{	

// Work-around until we can figure out why the right shader causes
// the GeForce driver to go tango uniform on OS X 10.6.8 only
//
#if GE_FORCE_WORKAROUND
		gObjectFullbrightAlphaMaskProgram.bind();
		gObjectFullbrightAlphaMaskProgram.uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
#else
		if (LLPipeline::sUnderWaterRender)
		{
			gDeferredFullbrightAlphaMaskWaterProgram.bind();
			gDeferredFullbrightAlphaMaskWaterProgram.uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
		}
		else
		{
			gDeferredFullbrightAlphaMaskProgram.bind();
			gDeferredFullbrightAlphaMaskProgram.uniform1f(LLShaderMgr::TEXTURE_GAMMA, 2.2f);
		}
#endif
	}

}

void LLDrawPoolFullbrightAlphaMask::renderPostDeferred(S32 pass)
{
	LLFastTimer t(FTM_RENDER_FULLBRIGHT);
	LLGLDisable blend(GL_BLEND);
	U32 fullbright_mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_COLOR | LLVertexBuffer::MAP_TEXTURE_INDEX;
	pushMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, fullbright_mask, TRUE, TRUE);
}

void LLDrawPoolFullbrightAlphaMask::endPostDeferredPass(S32 pass)
{
	if (LLPipeline::sRenderingHUDs || !LLPipeline::sRenderDeferred)
	{
		gObjectFullbrightAlphaMaskProgram.unbind();
	}
	else
	{

// Work-around until we can figure out why the right shader causes
// the GeForce driver to go tango uniform on OS X 10.6.8 only
//
#if GE_FORCE_WORKAROUND		
		gObjectFullbrightAlphaMaskProgram.unbind();
#else
		if (LLPipeline::sUnderWaterRender)
		{
			gDeferredFullbrightAlphaMaskWaterProgram.unbind();
		}
		else
		{
			gDeferredFullbrightAlphaMaskProgram.unbind();
		}
#endif

	}
	LLRenderPass::endRenderPass(pass);
}


