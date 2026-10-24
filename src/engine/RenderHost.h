// What a render pass needs from the application, and the only thing engine/
// knows about the composition root.
//
// The worker takes this interface instead of an App& , so the render loop cannot
// reach into unrelated state. App implements it (see app/AppState.h), which is
// the whole of the coupling that remains.
//
// The chain is borrowed rather than copied: an Effect instance is not copyable,
// so a caller that mutates the chain must call RenderWorker::waitIdle() first.

#pragma once

#include "core/Image.h"
#include "model/Chain.h"

#include <string>

class RenderHost {
 public:
  virtual ~RenderHost() = default;

  // The chain to run. Borrowed; see the note above.
  virtual const ChainState &chain() const = 0;

  virtual const Image &fullImage() const = 0;
  virtual const Image &previewImage() const = 0;
  // Rewritten by RenderWorker::rebuildPreview, which waits out the worker first.
  virtual Image &mutablePreviewImage() = 0;

  virtual ColorSpace workingSpace() const = 0;
  virtual ColorSpace outputSpace() const = 0;
  virtual ColorSpace previewSpace() const = 0;

  // Long edge of the full image, so a pass can report its proxy scale.
  virtual int projectLongEdge() const = 0;

  // Long-edge cap the interface currently asks for; "Fit to preview" moves it.
  virtual int wantedPreviewMaxEdge() const = 0;
  virtual bool quitting() const = 0;
  virtual void reportStatus(const std::string &text) = 0;
};
