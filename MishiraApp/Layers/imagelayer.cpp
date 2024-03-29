//*****************************************************************************
// Mishira: An audiovisual production tool for broadcasting live video
//
// Copyright (C) 2014 Lucas Murray <lucas@polyflare.com>
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//*****************************************************************************

#include "imagelayer.h"
#include "application.h"
#include "fileimagetexture.h"
#include "imagelayerdialog.h"
#include "layergroup.h"
#include "profile.h"

const QString LOG_CAT = QStringLiteral("Scene");

ImageLayer::ImageLayer(LayerGroup *parent)
	: Layer(parent)
	, m_vertBuf(vidgfx_texdecalbuf_new(NULL))
	, m_imgTex(NULL)
	, m_filenameChanged(true)

	// Settings
	, m_filename()
	, m_scrollSpeed()
{
	// As `render()` can be called multiple times on the same layer (Switching
	// scenes with a shared layer) we must do time-based calculations
	// separately
	connect(App, &Application::queuedFrameEvent,
		this, &ImageLayer::queuedFrameEvent);
}

ImageLayer::~ImageLayer()
{
	if(m_imgTex != NULL) {
		// Should never happen
		delete m_imgTex;
		m_imgTex = false;
	}

	vidgfx_texdecalbuf_destroy(m_vertBuf);
}

void ImageLayer::setFilename(const QString &filename)
{
	// Do not check if the filename is the same as the file contents might have
	// changed and the user wants to reload it
	//if(m_filename == filename)
	//	return; // Nothing to do
	m_filename = filename;
	m_filenameChanged = true;

	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void ImageLayer::setScrollSpeed(const QPoint &speed)
{
	if(m_scrollSpeed == speed)
		return; // Nothing to do
	m_scrollSpeed = speed;
	if(m_scrollSpeed.x() == 0 || m_scrollSpeed.y() == 0) {
		// Make changing the speed nicer visually by only resetting it when
		// required.
		vidgfx_texdecalbuf_reset_scrolling(m_vertBuf);
	}

	updateResourcesIfLoaded();
	m_parent->layerChanged(this); // Remote emit
}

void ImageLayer::showEvent()
{
	// Animations should completely reset when visiblity changes
	if(m_imgTex == NULL)
		return;
	m_imgTex->setAnimationPaused(false);
	if(m_imgTex->resetAnimation()) // Behave like we reloaded the file
		textureMaybeChanged();
}

void ImageLayer::hideEvent()
{
	if(m_imgTex == NULL)
		return;
	m_imgTex->setAnimationPaused(true);
}

void ImageLayer::parentShowEvent()
{
	showEvent();
}

void ImageLayer::parentHideEvent()
{
	hideEvent();
}

void ImageLayer::initializeResources(VidgfxContext *gfx)
{
	appLog(LOG_CAT)
		<< "Creating hardware resources for layer " << getIdString();

	// Start invisible
	setVisibleRect(QRect());

	// Reset caching and update all resources
	m_filenameChanged = true;
	vidgfx_texdecalbuf_set_context(m_vertBuf, gfx);
	vidgfx_texdecalbuf_set_rect(m_vertBuf, QRectF());
	vidgfx_texdecalbuf_set_tex_uv(m_vertBuf, QRectF());
	updateResources(gfx);
}

void ImageLayer::updateResources(VidgfxContext *gfx)
{
	// Recreate texture only if it's changed
	if(m_filenameChanged) {
		m_filenameChanged = false;
		if(m_imgTex != NULL) {
			delete m_imgTex;
			m_imgTex = NULL;
		}
		if(m_filename.isEmpty())
			return; // No file specified
		m_imgTex = new FileImageTexture(m_filename);
	}

	// Update the vertex buffer and visability rectangle
	textureMaybeChanged();
}

void ImageLayer::textureMaybeChanged()
{
	// Update vertex buffer and visible rectangle
	if(m_imgTex == NULL) {
		setVisibleRect(QRect()); // Layer is invisible
		return;
	}
	VidgfxTex *tex = m_imgTex->getTexture();
	if(tex == NULL) {
		setVisibleRect(QRect()); // Layer is invisible
		return;
	}
	const QRectF rect = scaledRectFromActualSize(vidgfx_tex_get_size(tex));
	vidgfx_texdecalbuf_set_rect(m_vertBuf, rect);
	setVisibleRect(rect.toAlignedRect());
}

void ImageLayer::destroyResources(VidgfxContext *gfx)
{
	appLog(LOG_CAT)
		<< "Destroying hardware resources for layer " << getIdString();

	vidgfx_texdecalbuf_destroy_vert_buf(m_vertBuf);
	vidgfx_texdecalbuf_set_context(m_vertBuf, NULL);
	if(m_imgTex != NULL) {
		delete m_imgTex;
		m_imgTex = NULL;
	}
}

void ImageLayer::render(
	VidgfxContext *gfx, Scene *scene, uint frameNum, int numDropped)
{
	if(m_imgTex == NULL)
		return; // Nothing to render
	VidgfxTex *tex = m_imgTex->getTexture();
	if(tex == NULL)
		return; // Image not loaded yet or an error occurred during load

	// Prepare texture for render. TODO: If the image doesn't have animation
	// then we can optimize rendering by caching the prepared texture data. We
	// still need to keep the original though just in case the user resizes the
	// layer.
	// TODO: Filter mode selection and orientation
	QPointF pxSize, botRight;
	tex = vidgfx_context_prepare_tex(
		gfx, tex, getVisibleRect().size(), GfxBilinearFilter, true,
		pxSize, botRight);
	vidgfx_texdecalbuf_set_tex_uv(
		m_vertBuf, QPointF(), botRight, GfxUnchangedOrient);
	vidgfx_context_set_tex(gfx, tex);

	// Do the actual render
	VidgfxVertBuf *vertBuf = vidgfx_texdecalbuf_get_vert_buf(m_vertBuf);
	if(vertBuf != NULL) {
		vidgfx_context_set_shader(gfx, GfxTexDecalShader);
		vidgfx_context_set_topology(
			gfx, vidgfx_texdecalbuf_get_topology(m_vertBuf));
		QColor prevCol = vidgfx_context_get_tex_decal_mod_color(gfx);
		vidgfx_context_set_tex_decal_mod_color(
			gfx, QColor(255, 255, 255, (int)(getOpacity() * 255.0f)));
		if(m_imgTex->hasTransparency() || getOpacity() != 1.0f)
			vidgfx_context_set_blending(gfx, GfxAlphaBlending);
		else
			vidgfx_context_set_blending(gfx, GfxNoBlending);
		vidgfx_context_draw_buf(gfx, vertBuf);
		vidgfx_context_set_tex_decal_mod_color(gfx, prevCol);
	}
}

LyrType ImageLayer::getType() const
{
	return LyrImageLayerType;
}

bool ImageLayer::hasSettingsDialog()
{
	return true;
}

LayerDialog *ImageLayer::createSettingsDialog(QWidget *parent)
{
	return new ImageLayerDialog(this, parent);
}

void ImageLayer::serialize(QDataStream *stream) const
{
	Layer::serialize(stream);

	// Write data version number
	*stream << (quint32)1;

	// Save our data
	*stream << m_filename;
	*stream << m_scrollSpeed;
}

bool ImageLayer::unserialize(QDataStream *stream)
{
	if(!Layer::unserialize(stream))
		return false;

	quint32 version;

	// Read data version number
	*stream >> version;

	// Read our data
	if(version >= 0 && version <= 1) {
		*stream >> m_filename;
		if(version >= 1)
			*stream >> m_scrollSpeed;
		else
			m_scrollSpeed = QPoint(0, 0);
	} else {
		appLog(LOG_CAT, Log::Warning)
			<< "Unknown version number in image layer serialized data, "
			<< "cannot load settings";
		return false;
	}

	return true;
}

void ImageLayer::queuedFrameEvent(uint frameNum, int numDropped)
{
	if(!isVisibleSomewhere())
		return;
	if(m_imgTex == NULL)
		return; // Nothing to do

	// Forward to image texture helper in order for animations to process.
	// TODO: We most likely process this AFTER the frame we want due to Qt
	// signal processing order
	if(m_imgTex->processFrameEvent(frameNum, numDropped))
		textureMaybeChanged();

	if(m_scrollSpeed.isNull() || getVisibleRect().isEmpty())
		return; // Nothing to do

	// Calculate amount to scroll in UV coordinates per frame
	Fraction framerate = App->getProfile()->getVideoFramerate();
	QPointF scrollPerFrame(
		(qreal)m_scrollSpeed.x() / framerate.asFloat() /
		(qreal)getVisibleRect().width(),
		(qreal)m_scrollSpeed.y() / framerate.asFloat() /
		(qreal)getVisibleRect().height());

	// Apply scroll
	QPointF scroll = scrollPerFrame * (qreal)(numDropped + 1);
	vidgfx_texdecalbuf_scroll_by(m_vertBuf, scroll);
}
