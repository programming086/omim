#include "drape_frontend/backend_renderer.hpp"
#include "drape_frontend/read_manager.hpp"
#include "drape_frontend/batchers_pool.hpp"
#include "drape_frontend/visual_params.hpp"
#include "drape_frontend/map_shape.hpp"

#include "drape_frontend/threads_commutator.hpp"
#include "drape_frontend/message_subclasses.hpp"

#include "drape/oglcontextfactory.hpp"
#include "drape/texture_manager.hpp"

#include "platform/platform.hpp"

#include "std/bind.hpp"

namespace df
{

BackendRenderer::BackendRenderer(dp::RefPointer<ThreadsCommutator> commutator,
                                 dp::RefPointer<dp::OGLContextFactory> oglcontextfactory,
                                 MapDataProvider const & model)
  : m_model(model)
  , m_engineContext(commutator)
  , m_commutator(commutator)
  , m_contextFactory(oglcontextfactory)
  , m_textures(new dp::TextureManager())
{
  m_commutator->RegisterThread(ThreadsCommutator::ResourceUploadThread, this);
  m_batchersPool.Reset(new BatchersPool(ReadManager::ReadCount(), bind(&BackendRenderer::FlushGeometry, this, _1)));
  m_readManager.Reset(new ReadManager(m_engineContext, m_model));

  StartThread();
}

BackendRenderer::~BackendRenderer()
{
  StopThread();
}

/////////////////////////////////////////
//           MessageAcceptor           //
/////////////////////////////////////////
void BackendRenderer::AcceptMessage(dp::RefPointer<Message> message)
{
  switch (message->GetType())
  {
  case Message::UpdateReadManager:
    {
      UpdateReadManagerMessage * msg = df::CastMessage<UpdateReadManagerMessage>(message);
      ScreenBase const & screen = msg->GetScreen();
      set<TileKey> const & tiles = msg->GetTiles();
      m_readManager->UpdateCoverage(screen, tiles);
      break;
    }
  case Message::InvalidateReadManagerRect:
    {
      InvalidateReadManagerRectMessage * msg = df::CastMessage<InvalidateReadManagerRectMessage>(message);
      m_readManager->Invalidate(msg->GetTilesForInvalidate());
      break;
    }
  case Message::TileReadStarted:
    m_batchersPool->ReserveBatcher(df::CastMessage<BaseTileMessage>(message)->GetKey());
    break;
  case Message::TileReadEnded:
    m_batchersPool->ReleaseBatcher(df::CastMessage<BaseTileMessage>(message)->GetKey());
    break;
  case Message::MapShapeReaded:
    {
      MapShapeReadedMessage * msg = df::CastMessage<MapShapeReadedMessage>(message);
      dp::RefPointer<dp::Batcher> batcher = m_batchersPool->GetTileBatcher(msg->GetKey());
      dp::MasterPointer<MapShape> shape(msg->GetShape());
      shape->Draw(batcher, m_textures.GetRefPointer());

      shape.Destroy();
    }
    break;
  default:
    ASSERT(false, ());
    break;
  }
}

/////////////////////////////////////////
//             ThreadPart              //
/////////////////////////////////////////
void BackendRenderer::StartThread()
{
  m_selfThread.Create(make_unique<Routine>(*this));
}

void BackendRenderer::StopThread()
{
  m_selfThread.GetRoutine()->Cancel();
  CloseQueue();
  m_selfThread.Join();
}

void BackendRenderer::ReleaseResources()
{
  m_readManager->Stop();

  m_readManager.Destroy();
  m_batchersPool.Destroy();

  m_textures->Release();
  m_textures.Destroy();
}

BackendRenderer::Routine::Routine(BackendRenderer & renderer) : m_renderer(renderer) {}

void BackendRenderer::Routine::Do()
{
  m_renderer.m_contextFactory->getResourcesUploadContext()->makeCurrent();
  m_renderer.InitGLDependentResource();

  while (!IsCancelled())
    m_renderer.ProcessSingleMessage();

  m_renderer.ReleaseResources();
}

void BackendRenderer::InitGLDependentResource()
{
  dp::TextureManager::Params params;
  params.m_resPrefix = VisualParams::Instance().GetResourcePostfix();
  params.m_glyphMngParams.m_uniBlocks = "unicode_blocks.txt";
  params.m_glyphMngParams.m_whitelist = "fonts_whitelist.txt";
  params.m_glyphMngParams.m_blacklist = "fonts_blacklist.txt";
  GetPlatform().GetFontNames(params.m_glyphMngParams.m_fonts);

  m_textures->Init(params);
}

void BackendRenderer::FlushGeometry(dp::TransferPointer<Message> message)
{
  m_textures->UpdateDynamicTextures();
  GLFunctions::glFlush();
  m_commutator->PostMessage(ThreadsCommutator::RenderThread, message);
}

} // namespace df
