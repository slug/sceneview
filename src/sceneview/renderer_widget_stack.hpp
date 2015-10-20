#ifndef SCENEVIEW_RENDERER_WIDGET_STACK
#define SCENEVIEW_RENDERER_WIDGET_STACK

#include <QDockWidget>

#include <sceneview/renderer.hpp>

class QScrollArea;
class QVBoxLayout;

namespace sceneview {

class RendererWidgetStack : public QDockWidget {
  Q_OBJECT

  public:
    RendererWidgetStack(QWidget* parent = nullptr);

  public slots:
    void AddRendererWidget(Renderer* renderer);

  private:
    QScrollArea* area_;
    QWidget* container_;
    QVBoxLayout* layout_;
};

}  // namespace sceneview

#endif  // SCENEVIEW_RENDERER_WIDGET_STACK
