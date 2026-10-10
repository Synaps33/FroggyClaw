#include <SDL2/SDL.h>
#include "Scene.h"
#include "TilePlaneSceneNode.h"
#include "../Actor/Components/RenderComponent.h"
#include "../Graphics2D/Image.h"
#include "../GameApp/BaseGameApp.h"

SDL2TilePlaneSceneNode::SDL2TilePlaneSceneNode(const uint32 actorId,
    BaseRenderComponent* pRenderComponent,
    RenderPass renderPass,
    Point position)
    : SceneNode(actorId, pRenderComponent, renderPass, position)
{

}

SDL2TilePlaneSceneNode::~SDL2TilePlaneSceneNode()
{

}

void SDL2TilePlaneSceneNode::VRender(Scene* pScene)
{
    TilePlaneRenderComponent* pRenderComponent = static_cast<TilePlaneRenderComponent*>(m_pRenderComponent);

    const TilePlaneProperties* pProperties = pRenderComponent->GetTilePlaneProperties();
    const TileImageList* pImageList = pRenderComponent->GetTileImageList();

    shared_ptr<CameraNode> camera = pScene->GetCamera();
    SDL_Renderer* renderer = pScene->GetRenderer();

    // Multiple times user variables
    int32 tilePixelWidth = pProperties->tilePixelWidth;
    int32 tilePixelHeight = pProperties->tilePixelHeight;

    const int32_t numTilesPadding = 0;

    const SDL_Rect cameraRect = camera->GetCameraRect();

    float movementRatioX = pProperties->movementPercentX / 100.0f;
    float movementRatioY = pProperties->movementPercentY / 100.0f;

    float parallaxCameraPosX = (float) cameraRect.x * movementRatioX;
    float parallaxCameraPosY = (float) cameraRect.y * movementRatioY;

    int32_t startCol = (int32_t)(parallaxCameraPosX / tilePixelWidth) - numTilesPadding;
    int32_t startRow = (int32_t)(parallaxCameraPosY / tilePixelHeight) - numTilesPadding;

    // We need to add 2 due to startCol/startRow + colTilesToRender/rowTilesToRender float->int casting
    int32_t colTilesToRender = (uint32_t)(cameraRect.w / tilePixelWidth) + 2 + numTilesPadding;
    int32_t rowTilesToRender = (uint32_t)(cameraRect.h / tilePixelHeight) + 2 + numTilesPadding;

    // Some planes (Back, Front) repeat themselves, which means they can be rendered
    // even when out of bounds
    int32_t maxTileIdxX = pProperties->tilesOnAxisX;
    int32_t maxTileIdxY = pProperties->tilesOnAxisY;
    int32_t minTileIdxX = 0;
    int32_t minTileIdxY = 0;
    // TODO: Wrap even when when out of bounds on the negative side
    if (pProperties->isWrappedX)
    {
        maxTileIdxX = INT32_MAX;
        minTileIdxX = 0;
    }
    if (pProperties->isWrappedY)
    {
        maxTileIdxY = INT32_MAX;
        minTileIdxY = 0;
    }

    int32_t endRow = startRow + rowTilesToRender;
    /* Wrapped planes set maxTileIdx to INT32_MAX as a "no clamp" sentinel;
     * adding 1 to it is signed overflow (UB) and wrapped to INT32_MIN here,
     * making the render loops below execute zero times - the whole
     * parallax Background plane rendered as black. Skip the clamp entirely
     * for wrapped planes; the modulo inside the loop handles the wrap. */
    if (maxTileIdxY != INT32_MAX && endRow > maxTileIdxY + 1) endRow = maxTileIdxY + 1;
    if (!pProperties->isWrappedY && startRow < minTileIdxY) startRow = minTileIdxY;

    int32_t endCol = startCol + colTilesToRender;
    if (maxTileIdxX != INT32_MAX && endCol > maxTileIdxX + 1) endCol = maxTileIdxX + 1;
    if (!pProperties->isWrappedX && startCol < minTileIdxX) startCol = minTileIdxX;

    const int32_t tilesOnX = pProperties->tilesOnAxisX;
    const int32_t tilesOnY = pProperties->tilesOnAxisY;
    const auto& imgList = *pImageList;

    /* One-shot diagnostics for the handheld black-background report: tells
     * us whether the plane draws zero tiles (bounds), null images (load
     * failure) or renders normally. Reaches Multicore.log via SDL_Log. */
    {
        static int s_diag_budget = 8;
        if (s_diag_budget > 0)
        {
            int nonNull = 0;
            for (size_t i = 0; i < imgList.size(); i++)
                if (imgList[i] && imgList[i]->GetTexture() != NULL) nonNull++;
            SDL_Log("PLANE %s: cam=%d,%d,%d,%d par=%.0f,%.0f range c%d..%d r%d..%d tilesOn=%dx%d imgList=%d nonNull=%d drawn_pass=%d",
                pProperties->name.c_str(), cameraRect.x, cameraRect.y, cameraRect.w, cameraRect.h,
                parallaxCameraPosX, parallaxCameraPosY,
                startCol, endCol, startRow, endRow, tilesOnX, tilesOnY,
                (int)imgList.size(), nonNull, (int)VGetProperties()->GetRenderPass());
            s_diag_budget--;
        }
    }

    for (int32_t row = startRow; row < endRow; row++)
    {
        if (row < minTileIdxY)
        {
            continue;
        }
        const int rowOffset = (row % tilesOnY) * tilesOnX;
        const int32_t y = row * tilePixelHeight - parallaxCameraPosY;

        for (int32_t col = startCol; col < endCol; col++)
        {
            if (col < minTileIdxX)
            {
                continue;
            }
            const int colTileIndex = col % tilesOnX;

            Image* image = imgList[rowOffset + colTileIndex];

            if (image && image->GetTexture() != NULL)
            {
                int32_t x = col * tilePixelWidth - parallaxCameraPosX;
                SDL_Rect tileRect = { x,
                    y,
                    tilePixelWidth,
                    tilePixelHeight };

                SDL_RenderCopy(renderer, image->GetTexture(), NULL, &tileRect);
            }
        }
    }
}
