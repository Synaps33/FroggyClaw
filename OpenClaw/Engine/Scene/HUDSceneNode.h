#ifndef __HUDSCENENODE_H__
#define __HUDSCENENODE_H__

#include "../SharedDefines.h"
#include "../Scene/SceneNodes.h"

/* The in-game HUD was authored for a 640x480 PC monitor and the handheld
 * shrinks that whole camera image onto a 320x240 panel, which leaves the
 * icon frames and digits only a few pixels tall. Both HUD layers (the icon
 * frames rendered by SDL2HUDSceneNode and the digits in GameHUD.cpp) are
 * therefore magnified by this integer factor around the screen corner they
 * are anchored to, so they keep hugging the same corner and stay aligned
 * with each other. Keep it integral: the digit art is pixel art and a
 * fractional factor would blur it. */
static const int HUD_UI_SCALE = 2;

class SDL2HUDSceneNode : public SceneNode
{
public:
    SDL2HUDSceneNode(const uint32 actorId,
        BaseRenderComponent* pRenderComponent,
        RenderPass renderPass,
        Point position,
        bool visible);

    virtual ~SDL2HUDSceneNode();

    // Interface overrides
    virtual void VRender(Scene* pScene);
    virtual bool IsVisible(Scene* pScene) const { return m_IsActive; }
    virtual void SetVisible(bool visible) { m_IsActive = visible; }

protected:
    bool m_IsActive;
};

#endif